"""Named/linked component acquisition (Phase 5 checkpoint 5).

A user supplies a URL to a manufacturer product page instead of typing a
component's identity by hand. This fetches it through the safety-bounded
`outbound_fetch.fetch_url_safely`, retains the exact response bytes as an
immutable artifact via the same `store_submitted_artifact` the manual
entry path already uses, and proposes identity candidates (manufacturer,
part number) extracted from any `schema.org/Product` JSON-LD the page
embeds.

This intentionally does not create a component draft. Engineering
parameters (torque, current limits, and similar) are not present in
product-page JSON-LD; a human still submits those through the existing
`POST /api/v2/component-drafts` manual entry path, now optionally
pre-filled with the identity this proposes. Wiring the retained artifact
in as real claim evidence on that subsequent draft is separately scoped
future work -- `ManualComponentDraftRequestV2`'s parameter shape only
supports user-measurement evidence today.
"""

from __future__ import annotations

import re

from pydantic import HttpUrl
import sqlalchemy as sa
from sqlalchemy.orm import Session

from .artifact_store import store_submitted_artifact
from .canonical_json import canonicalize_value, object_hash
from .contracts_v2 import SCHEMA_VERSION, ContractV2
from .jsonld_extraction import extract_product_identity
from .models_v2 import ComponentAcquisitionJobV2
from .outbound_fetch import OutboundFetchError, fetch_url_safely

_IDEMPOTENCY_PATTERN = re.compile(r"[A-Za-z0-9._:-]{16,128}\Z")


class ComponentAcquisitionError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


class ComponentAcquisitionRequestV2(ContractV2):
    schema_version: str
    url: HttpUrl


def _request_fingerprint(canonical_request: bytes) -> str:
    return object_hash(canonical_request)


def _existing_result(
    job: ComponentAcquisitionJobV2, canonical_request: bytes
) -> ComponentAcquisitionJobV2:
    if job.request_fingerprint != _request_fingerprint(canonical_request):
        raise ComponentAcquisitionError(
            "acquisition_idempotency_conflict",
            "the idempotency key is already bound to a different "
            "component acquisition request",
        )
    return job


def create_component_acquisition(
    db: Session,
    *,
    request: ComponentAcquisitionRequestV2,
    idempotency_key: str,
) -> ComponentAcquisitionJobV2:
    """Create or replay one named/linked component acquisition."""

    if request.schema_version != SCHEMA_VERSION:
        raise ComponentAcquisitionError(
            "unsupported_schema",
            "the requested component acquisition schema version is not supported",
        )
    if not isinstance(idempotency_key, str) or not _IDEMPOTENCY_PATTERN.fullmatch(
        idempotency_key
    ):
        raise ComponentAcquisitionError(
            "invalid_idempotency_key",
            "component acquisition idempotency key must be 16-128 ASCII "
            "token characters",
        )
    canonical_request = canonicalize_value(
        request.model_dump(mode="json", by_alias=True)
    )

    with db.begin():
        existing = db.scalar(
            sa.select(ComponentAcquisitionJobV2).where(
                ComponentAcquisitionJobV2.idempotency_key == idempotency_key
            )
        )
        if existing is not None:
            return _existing_result(existing, canonical_request)

        job = ComponentAcquisitionJobV2(
            idempotency_key=idempotency_key,
            request_fingerprint=_request_fingerprint(canonical_request),
            url=str(request.url),
            status="queued",
            source_artifact_hash=None,
            extracted_manufacturer=None,
            extracted_part_number=None,
            extraction_method=None,
            error_code=None,
            error_message=None,
        )
        db.add(job)
        db.flush()

        try:
            fetched = fetch_url_safely(str(request.url))
        except OutboundFetchError as exc:
            raise ComponentAcquisitionError(exc.code, str(exc)) from exc

        job.status = "running"
        db.flush()

        artifact = store_submitted_artifact(
            db,
            payload_bytes=fetched.bytes,
            media_type=fetched.content_type,
            filename=f"component-acquisition-{job.id}.html",
        )

        identity = extract_product_identity(fetched.bytes)
        job.source_artifact_hash = artifact.object_hash
        job.extracted_manufacturer = identity.manufacturer if identity else None
        job.extracted_part_number = identity.part_number if identity else None
        job.extraction_method = "jsonld" if identity is not None else "none"
        job.status = "succeeded"
        db.flush()
        return job


__all__ = [
    "ComponentAcquisitionError",
    "ComponentAcquisitionRequestV2",
    "create_component_acquisition",
]
