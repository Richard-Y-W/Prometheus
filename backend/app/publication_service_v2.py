"""Atomic, replayable publication for reviewed Program 01A v2 drafts."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
import hashlib
import re

import sqlalchemy as sa
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session, sessionmaker

from .canonical_json import canonicalize_value, parse_strict_json
from .contracts_v2 import (
    PACKAGE_MEDIA_TYPE,
    SCHEMA_ID,
    SCHEMA_VERSION,
    ExecutionComponentV2,
    PublicationRequestV2,
)
from .db_types import utc_now
from .models_v1 import ComponentRevision
from .models_v2 import PublicationRequestV2 as PublicationRequestRecord
from .object_store import (
    PublishedObjectIntegrityError,
    load_verified_published_object,
    put_published_object,
)
from .package_compiler_v2 import (
    PackageCompilationError,
    compile_execution_component,
)
from .transaction import (
    MAX_POSTGRES_WRITE_ATTEMPTS,
    RetryableWriteError,
    RevisionLockError,
    locked_revision_transaction,
)


_OPERATION = "publication"
_IDEMPOTENCY_KEY = re.compile(r"[A-Za-z0-9._:-]{16,128}\Z")
_REVIEW_FAILURES = {
    "effective_review_missing",
    "effective_review_not_accepted",
    "review_fingerprint_mismatch",
    "review_version_invalid",
}
_GATE_FAILURES = {"publication_gate_missing", "publication_gate_unresolved"}


class PublicationStage(StrEnum):
    AFTER_IDEMPOTENCY_RESOLUTION = "after_idempotency_resolution"
    AFTER_DRAFT_VALIDATION = "after_draft_validation"
    AFTER_PACKAGE_COMPILATION = "after_package_compilation"
    AFTER_SCHEMA_VALIDATION = "after_schema_validation"
    AFTER_CANONICALIZATION = "after_canonicalization"
    AFTER_HASH_COMPUTATION = "after_hash_computation"
    AFTER_BYTE_VERIFICATION = "after_byte_verification"
    AFTER_OBJECT_INSERTION = "after_object_insertion"
    AFTER_REVISION_BINDING = "after_revision_binding"
    AFTER_RESPONSE_STORAGE = "after_response_storage"
    BEFORE_COMMIT = "before_commit"


@dataclass(frozen=True)
class StoredApplicationResponse:
    status_code: int
    body: bytes
    headers: dict[str, str]
    object_hash: str | None


class PublicationInputError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class _TerminalPublicationFailure(ValueError):
    def __init__(self, code: str, message: str, **context: object):
        super().__init__(message)
        self.code = code
        self.context = context


def _emit(
    stage_callback: Callable[[PublicationStage], None] | None,
    stage: PublicationStage,
) -> None:
    if stage_callback is not None:
        stage_callback(stage)


def _request_fingerprint(
    revision_id: str, request: PublicationRequestV2
) -> str:
    value = {
        "operation": _OPERATION,
        "revision_id": revision_id,
        "expected_draft_version": request.expected_draft_version,
        "schema_id": request.schema_id,
        "schema_version": request.schema_version,
    }
    source = canonicalize_value(value)
    return f"sha256:{hashlib.sha256(source).hexdigest()}"


def _error_response(
    code: str, message: str, **context: object
) -> StoredApplicationResponse:
    return StoredApplicationResponse(
        status_code=409,
        body=canonicalize_value(
            {"detail": {"code": code, "message": message, **context}}
        ),
        headers={"Content-Type": "application/json"},
        object_hash=None,
    )


def _idempotency_conflict() -> StoredApplicationResponse:
    return _error_response(
        "idempotency_conflict",
        "The idempotency key is already bound to a different publication request.",
    )


def _stored_response(record: PublicationRequestRecord) -> StoredApplicationResponse:
    if (
        record.response_status is None
        or record.response_body is None
        or record.response_headers is None
    ):
        raise RuntimeError("terminal publication request has no stored response")
    headers = record.response_headers
    if type(headers) is not dict or any(
        type(key) is not str or type(value) is not str
        for key, value in headers.items()
    ):
        raise RuntimeError("terminal publication request has invalid headers")
    return StoredApplicationResponse(
        status_code=record.response_status,
        body=bytes(record.response_body),
        headers=dict(headers),
        object_hash=record.published_object_hash,
    )


def _expected_success_response(
    *, revision_id: str, object_hash: str, execution_readiness: str
) -> StoredApplicationResponse:
    return StoredApplicationResponse(
        status_code=201,
        body=canonicalize_value(
            {
                "execution_readiness": execution_readiness,
                "media_type": PACKAGE_MEDIA_TYPE,
                "object_hash": object_hash,
                "publication_integrity": "sealed_v2",
                "revision_id": revision_id,
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "status": "published",
            }
        ),
        headers={
            "Content-Type": "application/json",
            "ETag": f'"{object_hash}"',
            "Location": f"/api/v2/revisions/{revision_id}/execution-package",
        },
        object_hash=object_hash,
    )


def _verify_success_replay(
    db: Session,
    *,
    revision: ComponentRevision,
    record: PublicationRequestRecord,
) -> StoredApplicationResponse:
    object_hash = record.published_object_hash
    if object_hash is None:
        raise PublishedObjectIntegrityError(
            "published_object_missing",
            "succeeded publication request has no object binding",
        )
    stored_object = load_verified_published_object(db, object_hash)
    try:
        package = ExecutionComponentV2.model_validate(
            parse_strict_json(bytes(stored_object.payload_bytes))
        )
    except (TypeError, ValueError) as exc:
        raise PublishedObjectIntegrityError(
            "published_object_schema_invalid",
            "published object does not satisfy the bound v2 contract",
        ) from exc
    if (
        str(package.revision_id) != revision.id
        or revision.status != "published"
        or revision.publication_integrity != "sealed_v2"
        or revision.content_hash != object_hash
        or revision.published_object_hash != object_hash
        or revision.published_at is None
    ):
        raise PublishedObjectIntegrityError(
            "published_object_binding_mismatch",
            "succeeded publication request does not match its sealed revision",
        )
    expected = _expected_success_response(
        revision_id=revision.id,
        object_hash=object_hash,
        execution_readiness=package.execution_readiness,
    )
    stored = _stored_response(record)
    if stored != expected:
        raise PublishedObjectIntegrityError(
            "published_response_mismatch",
            "stored publication response does not match the sealed object",
        )
    return stored


def _preflight_conflict(
    session_factory: sessionmaker,
    *,
    idempotency_key: str,
    fingerprint: str,
) -> bool:
    """Detect a lifetime key conflict before resolving a possibly new revision."""

    with session_factory() as db:
        existing = db.scalar(
            sa.select(PublicationRequestRecord).where(
                PublicationRequestRecord.operation == _OPERATION,
                PublicationRequestRecord.idempotency_key == idempotency_key,
            )
        )
        return existing is not None and existing.request_fingerprint != fingerprint


def _load_request_for_update(
    db: Session, idempotency_key: str
) -> PublicationRequestRecord | None:
    statement = sa.select(PublicationRequestRecord).where(
        PublicationRequestRecord.operation == _OPERATION,
        PublicationRequestRecord.idempotency_key == idempotency_key,
    )
    if db.get_bind().dialect.name == "postgresql":
        statement = statement.with_for_update()
    return db.scalar(statement)


def _resolve_or_insert_request(
    db: Session,
    *,
    revision_id: str,
    idempotency_key: str,
    fingerprint: str,
) -> tuple[PublicationRequestRecord | None, StoredApplicationResponse | None]:
    existing = _load_request_for_update(db, idempotency_key)
    if existing is not None:
        if existing.request_fingerprint != fingerprint:
            return None, _idempotency_conflict()
        if existing.state == "terminal_failure":
            return existing, _stored_response(existing)
        if existing.state == "succeeded":
            return existing, None
        raise RetryableWriteError(
            "publication_in_progress",
            "a matching publication request is still in progress",
        )

    record = PublicationRequestRecord(
        operation=_OPERATION,
        idempotency_key=idempotency_key,
        revision_id=revision_id,
        request_fingerprint=fingerprint,
        state="in_progress",
        response_status=None,
        response_body=None,
        response_headers=None,
        published_object_hash=None,
    )
    try:
        with db.begin_nested():
            db.add(record)
            db.flush()
    except IntegrityError:
        existing = _load_request_for_update(db, idempotency_key)
        if existing is None:
            raise
        if existing.request_fingerprint != fingerprint:
            return None, _idempotency_conflict()
        if existing.state == "terminal_failure":
            return existing, _stored_response(existing)
        if existing.state == "succeeded":
            return existing, None
        raise RetryableWriteError(
            "publication_in_progress",
            "a matching publication request is still in progress",
        )
    return record, None


def _require_publishable_draft(
    revision: ComponentRevision, request: PublicationRequestV2
) -> None:
    if (
        revision.status == "published"
        or revision.publication_integrity == "sealed_v2"
    ):
        raise _TerminalPublicationFailure(
            "revision_already_published",
            "The revision is already bound to an immutable published object.",
            published_object_hash=revision.published_object_hash,
        )
    if request.expected_draft_version != revision.draft_version:
        raise _TerminalPublicationFailure(
            "stale_draft_version",
            "The publication request expected a different draft version.",
            current_draft_version=revision.draft_version,
        )
    if request.schema_id != SCHEMA_ID or request.schema_version != SCHEMA_VERSION:
        raise _TerminalPublicationFailure(
            "unsupported_schema",
            "The requested execution-component schema is not supported.",
        )
    if (
        revision.status != "draft"
        or revision.publication_integrity != "v2_draft"
        or revision.contract_schema_id != SCHEMA_ID
        or revision.contract_schema_version != SCHEMA_VERSION
        or revision.draft_version is None
        or revision.draft_version < 0
    ):
        raise _TerminalPublicationFailure(
            "execution_package_invalid",
            "The revision is not a valid publishable v2 draft.",
        )


def _map_compilation_failure(
    error: PackageCompilationError,
) -> _TerminalPublicationFailure:
    if error.code in _REVIEW_FAILURES:
        return _TerminalPublicationFailure(
            "publication_review_incomplete",
            "Every selected claim requires an effective accepted review.",
        )
    if error.code in _GATE_FAILURES:
        return _TerminalPublicationFailure(
            "publication_gate_blocked",
            "A required publication gate is missing or unresolved.",
        )
    if error.code == "unsupported_schema":
        return _TerminalPublicationFailure(
            "unsupported_schema",
            "The requested execution-component schema is not supported.",
        )
    return _TerminalPublicationFailure(
        "execution_package_invalid",
        "The reviewed draft cannot be compiled into a valid execution package.",
    )


def _store_terminal_failure(
    db: Session,
    record: PublicationRequestRecord,
    failure: _TerminalPublicationFailure,
) -> StoredApplicationResponse:
    response = _error_response(failure.code, str(failure), **failure.context)
    record.state = "terminal_failure"
    record.response_status = response.status_code
    record.response_body = response.body
    record.response_headers = response.headers
    record.published_object_hash = None
    db.flush()
    return response


def _compiler_callback(
    stage_callback: Callable[[PublicationStage], None] | None,
) -> Callable[[str], None] | None:
    if stage_callback is None:
        return None

    def forward(stage: str) -> None:
        stage_callback(PublicationStage(stage))

    return forward


def _publish_once(
    *,
    revision_id: str,
    idempotency_key: str,
    fingerprint: str,
    request: PublicationRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[PublicationStage], None] | None,
) -> StoredApplicationResponse:
    with locked_revision_transaction(
        session_factory,
        revision_id,
        operation="publication",
    ) as (db, revision):
        record, replay = _resolve_or_insert_request(
            db,
            revision_id=revision_id,
            idempotency_key=idempotency_key,
            fingerprint=fingerprint,
        )
        _emit(stage_callback, PublicationStage.AFTER_IDEMPOTENCY_RESOLUTION)
        if replay is not None:
            return replay
        if record is None:
            return _idempotency_conflict()
        if record.state == "succeeded":
            try:
                return _verify_success_replay(
                    db, revision=revision, record=record
                )
            except PublishedObjectIntegrityError:
                return _error_response(
                    "published_object_integrity_error",
                    "The stored published object failed integrity verification.",
                )

        try:
            _require_publishable_draft(revision, request)
            _emit(stage_callback, PublicationStage.AFTER_DRAFT_VALIDATION)
            try:
                compiled = compile_execution_component(
                    db,
                    revision_id,
                    schema_id=request.schema_id,
                    schema_version=request.schema_version,
                    stage_callback=_compiler_callback(stage_callback),
                )
            except PackageCompilationError as exc:
                raise _map_compilation_failure(exc) from exc
            try:
                put_published_object(
                    db,
                    canonical_bytes=compiled.canonical_bytes,
                    expected_object_hash=compiled.object_hash,
                    media_type=PACKAGE_MEDIA_TYPE,
                    schema_id=SCHEMA_ID,
                    schema_version=SCHEMA_VERSION,
                    canonicalization="RFC8785",
                )
            except PublishedObjectIntegrityError as exc:
                raise _TerminalPublicationFailure(
                    "published_object_integrity_error",
                    "The immutable object store rejected the compiled package.",
                ) from exc
            _emit(stage_callback, PublicationStage.AFTER_OBJECT_INSERTION)

            revision.status = "published"
            revision.publication_integrity = "sealed_v2"
            revision.content_hash = compiled.object_hash
            revision.published_object_hash = compiled.object_hash
            revision.published_at = utc_now()
            db.flush()
            _emit(stage_callback, PublicationStage.AFTER_REVISION_BINDING)

            response = _expected_success_response(
                revision_id=revision_id,
                object_hash=compiled.object_hash,
                execution_readiness=compiled.execution_readiness,
            )
            record.state = "succeeded"
            record.response_status = response.status_code
            record.response_body = response.body
            record.response_headers = response.headers
            record.published_object_hash = compiled.object_hash
            db.flush()
            _emit(stage_callback, PublicationStage.AFTER_RESPONSE_STORAGE)
            _emit(stage_callback, PublicationStage.BEFORE_COMMIT)
            return response
        except _TerminalPublicationFailure as failure:
            return _store_terminal_failure(db, record, failure)


def _after_commit(_response: StoredApplicationResponse) -> None:
    """Test seam for a transport loss after durability but before observation."""


def publish_revision(
    *,
    revision_id: str,
    idempotency_key: str,
    request: PublicationRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[PublicationStage], None] | None = None,
) -> StoredApplicationResponse:
    """Seal one reviewed draft or replay its durable application response."""

    if type(idempotency_key) is not str or _IDEMPOTENCY_KEY.fullmatch(
        idempotency_key
    ) is None:
        raise PublicationInputError(
            "invalid_idempotency_key",
            "idempotency key must be 16-128 characters from [A-Za-z0-9._:-]",
        )
    fingerprint = _request_fingerprint(revision_id, request)
    if _preflight_conflict(
        session_factory,
        idempotency_key=idempotency_key,
        fingerprint=fingerprint,
    ):
        return _idempotency_conflict()

    bind = session_factory.kw.get("bind")
    dialect = bind.dialect.name if bind is not None else None
    maximum_attempts = (
        MAX_POSTGRES_WRITE_ATTEMPTS if dialect == "postgresql" else 1
    )
    last_retryable: RetryableWriteError | None = None
    for _attempt in range(maximum_attempts):
        try:
            response = _publish_once(
                revision_id=revision_id,
                idempotency_key=idempotency_key,
                fingerprint=fingerprint,
                request=request,
                session_factory=session_factory,
                stage_callback=stage_callback,
            )
            _after_commit(response)
            return response
        except RevisionLockError as exc:
            raise PublicationInputError(exc.code, str(exc)) from exc
        except RetryableWriteError as exc:
            last_retryable = exc
    assert last_retryable is not None
    raise last_retryable


__all__ = [
    "PublicationInputError",
    "PublicationStage",
    "StoredApplicationResponse",
    "publish_revision",
]
