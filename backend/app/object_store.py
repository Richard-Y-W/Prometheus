"""Immutable content-addressed storage for published v2 package bytes."""

from __future__ import annotations

from collections.abc import Callable
import re

from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from .canonical_json import CanonicalJsonError, object_hash, verify_canonical_bytes
from .contracts_v2 import PACKAGE_MEDIA_TYPE, SCHEMA_ID, SCHEMA_VERSION
from .models_v2 import PublishedObject


_HASH_PATTERN = re.compile(r"sha256:[0-9a-f]{64}\Z")


class PublishedObjectIntegrityError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _fail(code: str, message: str, cause: BaseException | None = None):
    error = PublishedObjectIntegrityError(code, message)
    if cause is None:
        raise error
    raise error from cause


def _require_supported_metadata(
    *, media_type: str, schema_id: str, schema_version: str, canonicalization: str
) -> None:
    if (
        media_type != PACKAGE_MEDIA_TYPE
        or schema_id != SCHEMA_ID
        or schema_version != SCHEMA_VERSION
        or canonicalization != "RFC8785"
    ):
        _fail(
            "published_object_metadata_mismatch",
            "published object metadata does not match the supported v2 contract",
        )


def _require_canonical(source: bytes) -> bytes:
    if type(source) is not bytes:
        _fail(
            "published_object_noncanonical",
            "published object payload must be an immutable byte string",
        )
    try:
        return verify_canonical_bytes(source)
    except CanonicalJsonError as exc:
        _fail(
            "published_object_noncanonical",
            "published object payload is not strict RFC 8785 bytes",
            exc,
        )


def _require_valid_row(row: PublishedObject) -> PublishedObject:
    _require_supported_metadata(
        media_type=row.media_type,
        schema_id=row.schema_id,
        schema_version=row.schema_version,
        canonicalization=row.canonicalization,
    )
    payload = bytes(row.payload_bytes)
    if row.byte_length != len(payload):
        _fail(
            "published_object_metadata_mismatch",
            "published object byte length does not match its stored payload",
        )
    _require_canonical(payload)
    recomputed = object_hash(payload)
    if recomputed != row.object_hash:
        _fail(
            "published_object_hash_mismatch",
            "published object payload does not match its content address",
        )
    return row


def _require_exact_existing(
    row: PublishedObject,
    *,
    payload: bytes,
    media_type: str,
    schema_id: str,
    schema_version: str,
    canonicalization: str,
) -> PublishedObject:
    if bytes(row.payload_bytes) != payload:
        _fail(
            "published_object_hash_collision",
            "the content address already identifies different package bytes",
        )
    if (
        row.byte_length != len(payload)
        or row.media_type != media_type
        or row.schema_id != schema_id
        or row.schema_version != schema_version
        or row.canonicalization != canonicalization
    ):
        _fail(
            "published_object_metadata_mismatch",
            "the content address already has different package metadata",
        )
    return _require_valid_row(row)


def put_published_object(
    db: Session,
    *,
    canonical_bytes: bytes,
    expected_object_hash: str,
    media_type: str,
    schema_id: str,
    schema_version: str,
    canonicalization: str,
    hash_function: Callable[[bytes], str] = object_hash,
) -> PublishedObject:
    """Insert exact canonical bytes or reuse an exact immutable match."""

    _require_supported_metadata(
        media_type=media_type,
        schema_id=schema_id,
        schema_version=schema_version,
        canonicalization=canonicalization,
    )
    payload = _require_canonical(canonical_bytes)
    if not isinstance(expected_object_hash, str) or not _HASH_PATTERN.fullmatch(
        expected_object_hash
    ):
        _fail(
            "published_object_hash_mismatch",
            "expected published object hash is malformed",
        )
    try:
        recomputed = hash_function(payload)
    except (CanonicalJsonError, ValueError, TypeError) as exc:
        _fail(
            "published_object_hash_mismatch",
            "published object hash could not be recomputed",
            exc,
        )
    if (
        not isinstance(recomputed, str)
        or not _HASH_PATTERN.fullmatch(recomputed)
        or recomputed != expected_object_hash
    ):
        _fail(
            "published_object_hash_mismatch",
            "published object bytes do not match the expected content address",
        )

    existing = db.get(PublishedObject, expected_object_hash)
    if existing is not None:
        return _require_exact_existing(
            existing,
            payload=payload,
            media_type=media_type,
            schema_id=schema_id,
            schema_version=schema_version,
            canonicalization=canonicalization,
        )

    row = PublishedObject(
        object_hash=expected_object_hash,
        payload_bytes=payload,
        byte_length=len(payload),
        media_type=media_type,
        schema_id=schema_id,
        schema_version=schema_version,
        canonicalization=canonicalization,
    )
    try:
        with db.begin_nested():
            db.add(row)
            db.flush()
    except IntegrityError:
        existing = db.get(PublishedObject, expected_object_hash)
        if existing is None:
            raise
        return _require_exact_existing(
            existing,
            payload=payload,
            media_type=media_type,
            schema_id=schema_id,
            schema_version=schema_version,
            canonicalization=canonicalization,
        )
    return row


def load_verified_published_object(
    db: Session, object_hash_value: str
) -> PublishedObject:
    """Load an immutable object only after revalidating bytes and metadata."""

    row = db.get(PublishedObject, object_hash_value)
    if row is None:
        _fail(
            "published_object_missing",
            f"published object {object_hash_value!r} does not exist",
        )
    return _require_valid_row(row)


__all__ = [
    "PublishedObjectIntegrityError",
    "load_verified_published_object",
    "put_published_object",
]
