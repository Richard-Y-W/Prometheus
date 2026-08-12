"""Thin HTTP adapters for the sealed Program 01A v2 trust boundary."""

from __future__ import annotations

import re
from typing import Annotated, Literal

from fastapi import APIRouter, Depends, Header, HTTPException, Request, Response
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.exceptions import RequestValidationError
from pydantic import Field, StrictInt, StrictStr
import sqlalchemy as sa
from sqlalchemy.orm import Session
from starlette.responses import Response as StarletteResponse

from .canonical_json import canonicalize_value
from .claim_identity_v2 import claim_evidence_ids
from .contracts_v2 import (
    PACKAGE_MEDIA_TYPE,
    SCHEMA_ID,
    SCHEMA_VERSION,
    ContractV2,
    HashId,
    PublicationRequestV2,
    ReviewRequestV2,
    UtcDatetime,
    UuidV4,
)
from .database import SessionLocal, get_db
from .fixture_pipeline_v2 import (
    FIXTURE_ID,
    FixtureDraftError,
    create_fixture_draft,
)
from .models_v1 import Component, ComponentRevision, Manufacturer
from .models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimSelectionV2,
    FixtureIngestionJobV2,
    ParameterSlotV2,
)
from .object_store import PublishedObjectIntegrityError, load_verified_published_object
from .publication_service_v2 import PublicationInputError, publish_revision
from .review_service_v2 import ReviewServiceError, review_claims
from .transaction import RetryableWriteError


router = APIRouter(prefix="/api/v2", tags=["program-01a-v2"])
_IDEMPOTENCY_PATTERN = r"^[A-Za-z0-9._:-]{16,128}$"


class FixtureIngestionRequestV2(ContractV2):
    fixture_id: Annotated[
        StrictStr,
        Field(min_length=1, max_length=128, json_schema_extra={"enum": [FIXTURE_ID]}),
    ]
    schema_version: Annotated[
        StrictStr,
        Field(
            min_length=1,
            max_length=32,
            json_schema_extra={"enum": [SCHEMA_VERSION]},
        ),
    ]


class PublicationHttpRequestV2(ContractV2):
    expected_draft_version: Annotated[StrictInt, Field(ge=0)]
    schema_id: Annotated[
        StrictStr,
        Field(min_length=1, max_length=256, json_schema_extra={"enum": [SCHEMA_ID]}),
    ]
    schema_version: Annotated[
        StrictStr,
        Field(
            min_length=1,
            max_length=32,
            json_schema_extra={"enum": [SCHEMA_VERSION]},
        ),
    ]


class ContractIdentityResponseV2(ContractV2):
    schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"]
    schema_version: Literal["2.0.0"]


class ComponentIdentityResponseV2(ContractV2):
    component_id: UuidV4
    manufacturer: StrictStr
    part_number: StrictStr
    revision: StrictStr
    component_class: StrictStr


class SelectedClaimResponseV2(ContractV2):
    claim_id: UuidV4
    claim_fingerprint: HashId
    value_state: Literal["known", "unknown"]
    value: dict[str, object]
    unit: str | None
    original_value: str | None
    original_unit: str | None
    validity_conditions: list[str]
    provenance: str
    evidence_ids: list[UuidV4]


class ParameterResponseV2(ContractV2):
    slot_id: UuidV4
    name: str
    quantity: str
    dimension: str
    required_for_execution: bool
    selected_claim: SelectedClaimResponseV2


class CapabilityGateResponseV2(ContractV2):
    gate_id: UuidV4
    capability_id: str
    phase: Literal["publication", "execution"]
    required_review_type: str
    state: Literal["pending", "satisfied", "blocked"]
    satisfying_reference_ids: list[str]
    reason: str | None


class RevisionResponseV2(ContractV2):
    id: UuidV4
    status: Literal["draft", "published"]
    draft_version: Annotated[StrictInt, Field(ge=0)]
    contract: ContractIdentityResponseV2
    component: ComponentIdentityResponseV2
    parameters: list[ParameterResponseV2]
    capability_gates: list[CapabilityGateResponseV2]
    publication_integrity: Literal["v2_draft", "sealed_v2"]
    object_hash: HashId | None
    published_at: UtcDatetime | None


class FixtureIngestionResponseV2(ContractV2):
    id: UuidV4
    state: Literal["succeeded"]
    fixture_id: Literal["prometheus.pm-36-gm.fixture-2"]
    revision: RevisionResponseV2


class PublicationSuccessResponseV2(ContractV2):
    execution_readiness: Literal["ready", "blocked"]
    media_type: Literal[
        "application/vnd.prometheus.execution-component+json;version=2.0.0"
    ]
    object_hash: HashId
    publication_integrity: Literal["sealed_v2"]
    revision_id: UuidV4
    schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"]
    schema_version: Literal["2.0.0"]
    status: Literal["published"]


def _error(status: int, code: str, message: str, **context: object) -> HTTPException:
    return HTTPException(
        status_code=status,
        detail={"code": code, "message": message, **context},
    )


def _validated_key(idempotency_key: str) -> str:
    if re.fullmatch(_IDEMPOTENCY_PATTERN, idempotency_key) is None:
        raise _error(
            422,
            "invalid_idempotency_key",
            "Idempotency-Key must be 16-128 ASCII token characters.",
        )
    return idempotency_key


def _claim_value(claim: CandidateClaimV2) -> dict[str, object]:
    if claim.value_state == "known" and isinstance(claim.value, dict):
        return dict(claim.value)
    if claim.value_state == "unknown" and isinstance(claim.reason, str):
        return {"kind": "unknown", "reason": claim.reason}
    raise _error(
        409,
        "revision_integrity_failure",
        "A selected claim has an invalid known/unknown value shape.",
    )


def _revision_value(db: Session, revision_id: str) -> dict[str, object]:
    revision = db.get(ComponentRevision, revision_id)
    if revision is None:
        raise _error(404, "revision_not_found", "The revision does not exist.")
    if (
        revision.contract_schema_id != SCHEMA_ID
        or revision.contract_schema_version != SCHEMA_VERSION
        or revision.draft_version is None
        or revision.publication_integrity not in {"v2_draft", "sealed_v2"}
    ):
        raise _error(
            409,
            "revision_integrity_failure",
            "The revision is not a Program 01A v2 revision.",
        )
    component = db.get(Component, revision.component_id)
    if component is None:
        raise _error(
            409,
            "revision_integrity_failure",
            "The revision references a missing component.",
        )
    manufacturer = db.get(Manufacturer, component.manufacturer_id)
    if manufacturer is None:
        raise _error(
            409,
            "revision_integrity_failure",
            "The component references a missing manufacturer.",
        )

    slots = list(
        db.scalars(
            sa.select(ParameterSlotV2)
            .where(ParameterSlotV2.revision_id == revision_id)
            .order_by(ParameterSlotV2.name, ParameterSlotV2.id)
        )
    )
    selections = {
        selection.slot_id: selection.claim_id
        for selection in db.scalars(
            sa.select(ClaimSelectionV2).where(
                ClaimSelectionV2.revision_id == revision_id
            )
        )
    }
    if not slots or len(selections) != len(slots):
        raise _error(
            409,
            "revision_integrity_failure",
            "Every displayed parameter requires one selected claim.",
        )
    claims = {
        claim.id: claim
        for claim in db.scalars(
            sa.select(CandidateClaimV2).where(
                CandidateClaimV2.id.in_(list(selections.values()))
            )
        )
    }
    parameters: list[dict[str, object]] = []
    for slot in slots:
        claim = claims.get(selections[slot.id])
        if (
            claim is None
            or claim.revision_id != revision_id
            or claim.slot_id != slot.id
            or not claim.finalized
            or claim.fingerprint is None
        ):
            raise _error(
                409,
                "revision_integrity_failure",
                "A parameter selection does not identify a finalized same-revision claim.",
            )
        parameters.append(
            {
                "slot_id": slot.id,
                "name": slot.name,
                "quantity": slot.quantity,
                "dimension": slot.dimension,
                "required_for_execution": slot.required_for_execution,
                "selected_claim": {
                    "claim_id": claim.id,
                    "claim_fingerprint": claim.fingerprint,
                    "value_state": claim.value_state,
                    "value": _claim_value(claim),
                    "unit": claim.unit,
                    "original_value": claim.original_value,
                    "original_unit": claim.original_unit,
                    "validity_conditions": list(claim.validity_conditions),
                    "provenance": claim.provenance,
                    "evidence_ids": claim_evidence_ids(db, claim),
                },
            }
        )

    gate_order = {
        "component_identity": 0,
        "source_artifact": 1,
        "claim_selection": 2,
        "claim_review": 3,
        "package_consumer": 4,
    }
    gates = sorted(
        db.scalars(
            sa.select(CapabilityGateV2).where(
                CapabilityGateV2.revision_id == revision_id
            )
        ),
        key=lambda gate: (gate_order.get(gate.required_review_type, 99), gate.id),
    )
    return {
        "id": revision.id,
        "status": revision.status,
        "draft_version": revision.draft_version,
        "contract": {"schema_id": SCHEMA_ID, "schema_version": SCHEMA_VERSION},
        "component": {
            "component_id": component.id,
            "manufacturer": manufacturer.name,
            "part_number": component.part_number,
            "revision": revision.revision,
            "component_class": component.model_class,
        },
        "parameters": parameters,
        "capability_gates": [
            {
                "gate_id": gate.id,
                "capability_id": gate.capability_id,
                "phase": gate.phase,
                "required_review_type": gate.required_review_type,
                "state": gate.state,
                "satisfying_reference_ids": list(gate.satisfying_references),
                "reason": gate.reason,
            }
            for gate in gates
        ],
        "publication_integrity": revision.publication_integrity,
        "object_hash": revision.published_object_hash,
        "published_at": revision.published_at,
    }


def _ingestion_value(
    db: Session, job: FixtureIngestionJobV2
) -> dict[str, object]:
    if job.status != "succeeded" or job.revision_id is None:
        raise _error(
            409,
            "fixture_ingestion_incomplete",
            "The fixture ingestion has no completed revision.",
        )
    return {
        "id": job.id,
        "state": job.status,
        "fixture_id": job.fixture_id,
        "revision": _revision_value(db, job.revision_id),
    }


async def v2_request_validation_exception_handler(
    request: Request, error: RequestValidationError
) -> StarletteResponse:
    path = request.url.path
    if path != "/api/v2" and not path.startswith("/api/v2/"):
        return await request_validation_exception_handler(request, error)
    for item in error.errors():
        location = tuple(item.get("loc", ()))
        if len(location) >= 2 and location[0] == "header" and str(
            location[-1]
        ).lower() == "idempotency-key":
            body = canonicalize_value(
                {
                    "detail": {
                        "code": "idempotency_key_required",
                        "message": "Idempotency-Key is required for this operation.",
                    }
                }
            )
            return Response(content=body, status_code=422, media_type="application/json")

    body_value = error.body
    if request.url.path.endswith("/reviews") and isinstance(body_value, dict):
        decisions = body_value.get("decisions")
        if isinstance(decisions, list) and any(
            isinstance(decision, dict)
            and "field_name" in decision
            and "claim_id" not in decision
            for decision in decisions
        ):
            body = canonicalize_value(
                {
                    "detail": {
                        "code": "claim_id_required",
                        "message": "V2 review decisions must identify claim_id.",
                    }
                }
            )
            return Response(content=body, status_code=422, media_type="application/json")

    body = canonicalize_value(
        {
            "detail": {
                "code": "request_validation_error",
                "message": "The v2 request does not satisfy its transport contract.",
            }
        }
    )
    return Response(content=body, status_code=422, media_type="application/json")


@router.post(
    "/fixture-ingestions",
    status_code=201,
    response_model=FixtureIngestionResponseV2,
)
def create_fixture_ingestion(
    body: FixtureIngestionRequestV2,
    idempotency_key: Annotated[str, Header(alias="Idempotency-Key")],
    db: Session = Depends(get_db),
):
    key = _validated_key(idempotency_key)
    if body.fixture_id != FIXTURE_ID or body.schema_version != SCHEMA_VERSION:
        existing = db.scalar(
            sa.select(FixtureIngestionJobV2).where(
                FixtureIngestionJobV2.idempotency_key == key
            )
        )
        db.rollback()
        if existing is not None:
            raise _error(
                409,
                "idempotency_conflict",
                "The idempotency key is bound to a different fixture request.",
            )
        if body.schema_version != SCHEMA_VERSION:
            raise _error(
                422,
                "unsupported_schema",
                "The requested fixture schema version is not supported.",
            )
        raise _error(
            422,
            "unsupported_fixture_id",
            "The requested fixture identity is not supported.",
        )
    try:
        result = create_fixture_draft(
            db, fixture_id=body.fixture_id, idempotency_key=key
        )
    except FixtureDraftError as exc:
        code = (
            "idempotency_conflict"
            if exc.code == "fixture_idempotency_conflict"
            else exc.code
        )
        status = 422 if code in {"invalid_idempotency_key", "unsupported_fixture_id"} else 409
        raise _error(status, code, str(exc)) from exc
    return _ingestion_value(db, result.ingestion_job)


@router.get(
    "/fixture-ingestions/{ingestion_id}",
    response_model=FixtureIngestionResponseV2,
)
def get_fixture_ingestion(
    ingestion_id: str, db: Session = Depends(get_db)
):
    job = db.get(FixtureIngestionJobV2, ingestion_id)
    if job is None:
        raise _error(
            404,
            "fixture_ingestion_not_found",
            "The fixture ingestion does not exist.",
        )
    return _ingestion_value(db, job)


@router.get("/revisions/{revision_id}", response_model=RevisionResponseV2)
def get_revision(revision_id: str, db: Session = Depends(get_db)):
    return _revision_value(db, revision_id)


@router.post("/revisions/{revision_id}/reviews", response_model=RevisionResponseV2)
def review_revision(
    revision_id: str,
    body: ReviewRequestV2,
    db: Session = Depends(get_db),
):
    try:
        review_claims(
            revision_id=revision_id,
            request=body,
            session_factory=SessionLocal,
        )
    except ReviewServiceError as exc:
        status = 404 if exc.code in {"revision_not_found", "claim_not_found"} else 409
        context: dict[str, object] = {}
        if exc.current_draft_version is not None:
            context["current_draft_version"] = exc.current_draft_version
        raise _error(status, exc.code, str(exc), **context) from exc
    except RetryableWriteError as exc:
        raise _error(503, exc.code, str(exc)) from exc
    return _revision_value(db, revision_id)


@router.post(
    "/revisions/{revision_id}/publication",
    status_code=201,
    responses={201: {"model": PublicationSuccessResponseV2}},
)
def publish_revision_http(
    revision_id: str,
    body: PublicationHttpRequestV2,
    idempotency_key: Annotated[str, Header(alias="Idempotency-Key")],
):
    key = _validated_key(idempotency_key)
    request = PublicationRequestV2.model_construct(
        expected_draft_version=body.expected_draft_version,
        schema_id=body.schema_id,
        schema_version=body.schema_version,
    )
    try:
        stored = publish_revision(
            revision_id=revision_id,
            idempotency_key=key,
            request=request,
            session_factory=SessionLocal,
        )
    except PublicationInputError as exc:
        status = 404 if exc.code == "revision_not_found" else 422
        raise _error(status, exc.code, str(exc)) from exc
    except RetryableWriteError as exc:
        raise _error(503, exc.code, str(exc)) from exc
    return Response(
        content=stored.body,
        status_code=stored.status_code,
        headers=stored.headers,
        media_type=None,
    )


@router.get(
    "/revisions/{revision_id}/execution-package",
    responses={
        200: {
            "description": "Exact sealed RFC 8785 execution-component bytes.",
            "content": {PACKAGE_MEDIA_TYPE: {"schema": {"type": "object"}}},
        }
    },
)
def export_execution_package(
    revision_id: str, db: Session = Depends(get_db)
):
    revision = db.get(ComponentRevision, revision_id)
    if revision is None:
        raise _error(404, "revision_not_found", "The revision does not exist.")
    if revision.status != "published":
        raise _error(
            409,
            "revision_not_published",
            "Only a published revision has an execution package.",
        )
    if (
        revision.publication_integrity != "sealed_v2"
        or revision.published_object_hash is None
        or revision.content_hash != revision.published_object_hash
    ):
        raise _error(
            409,
            "publication_integrity_unsealed",
            "The historical revision has no sealed v2 object binding.",
        )
    try:
        stored = load_verified_published_object(db, revision.published_object_hash)
    except PublishedObjectIntegrityError as exc:
        raise _error(
            409,
            "published_object_integrity_error",
            "The stored published object failed integrity verification.",
        ) from exc
    return Response(
        content=bytes(stored.payload_bytes),
        status_code=200,
        headers={
            "Content-Type": PACKAGE_MEDIA_TYPE,
            "ETag": f'"{stored.object_hash}"',
        },
        media_type=None,
    )


__all__ = [
    "FixtureIngestionRequestV2",
    "FixtureIngestionResponseV2",
    "PublicationHttpRequestV2",
    "RevisionResponseV2",
    "router",
    "v2_request_validation_exception_handler",
]
