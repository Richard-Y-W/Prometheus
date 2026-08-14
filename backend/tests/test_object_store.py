from __future__ import annotations

import pytest
import sqlalchemy as sa

from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import PACKAGE_MEDIA_TYPE, SCHEMA_ID, SCHEMA_VERSION
from app.database import SessionLocal
from app.models_v2 import PublishedObject
from app.object_store import (
    PublishedObjectIntegrityError,
    load_verified_published_object,
    put_published_object,
)


def _canonical(value: object) -> tuple[bytes, str]:
    payload = canonicalize_value(value)
    return payload, object_hash(payload)


def _put(db, payload: bytes, identity: str, **overrides):
    arguments = {
        "canonical_bytes": payload,
        "expected_object_hash": identity,
        "media_type": PACKAGE_MEDIA_TYPE,
        "schema_id": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "canonicalization": "RFC8785",
    }
    arguments.update(overrides)
    return put_published_object(db, **arguments)


def _assert_code(code: str, operation) -> PublishedObjectIntegrityError:
    with pytest.raises(PublishedObjectIntegrityError) as captured:
        operation()
    assert captured.value.code == code
    return captured.value


def test_put_and_load_return_the_exact_canonical_blob():
    payload, identity = _canonical({"package": "immutable", "version": 2})
    with SessionLocal() as db:
        stored = _put(db, payload, identity)
        db.commit()
    with SessionLocal() as db:
        loaded = load_verified_published_object(db, identity)
        assert loaded.payload_bytes == payload
        assert loaded.object_hash == identity
        assert loaded.byte_length == len(payload)


def test_exact_match_reuses_one_object_row():
    payload, identity = _canonical({"same": "object"})
    with SessionLocal() as db:
        first = _put(db, payload, identity)
        second = _put(db, payload, identity)
        assert first is second
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1


@pytest.mark.parametrize(
    "override",
    [
        {"media_type": "application/json"},
        {"schema_id": "urn:prometheus:schema:execution-component:9.0.0"},
        {"schema_version": "9.0.0"},
        {"canonicalization": "sorted-json"},
    ],
)
def test_unsupported_object_metadata_is_rejected(override):
    payload, identity = _canonical({"metadata": "closed"})
    with SessionLocal() as db:
        _assert_code(
            "published_object_metadata_mismatch",
            lambda: _put(db, payload, identity, **override),
        )
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 0


def test_existing_object_metadata_mismatch_is_not_silently_reused():
    payload, identity = _canonical({"metadata": "exact"})
    with SessionLocal() as db:
        db.add(
            PublishedObject(
                object_hash=identity,
                payload_bytes=payload,
                byte_length=len(payload),
                media_type="application/json",
                schema_id=SCHEMA_ID,
                schema_version=SCHEMA_VERSION,
                canonicalization="RFC8785",
            )
        )
        db.flush()
        _assert_code(
            "published_object_metadata_mismatch",
            lambda: _put(db, payload, identity),
        )


def test_forced_same_hash_different_bytes_is_a_collision():
    first_bytes, first_hash = _canonical({"value": "first"})
    second_bytes, _second_hash = _canonical({"value": "second"})
    with SessionLocal() as db:
        _put(db, first_bytes, first_hash)
        _assert_code(
            "published_object_hash_collision",
            lambda: _put(
                db,
                second_bytes,
                first_hash,
                hash_function=lambda _payload: first_hash,
            ),
        )


def test_noncanonical_input_is_rejected_before_storage():
    source = b'{ "value": 1 }'
    with SessionLocal() as db:
        _assert_code(
            "published_object_noncanonical",
            lambda: _put(
                db, source, "sha256:" + "a" * 64, hash_function=lambda _: "sha256:" + "a" * 64
            ),
        )


def test_expected_hash_mismatch_is_rejected():
    payload, _identity = _canonical({"hash": "wrong expectation"})
    with SessionLocal() as db:
        _assert_code(
            "published_object_hash_mismatch",
            lambda: _put(db, payload, "sha256:" + "a" * 64),
        )


def test_prepared_incorrect_length_is_detected_on_load():
    payload, identity = _canonical({"length": "must match bytes"})
    with SessionLocal.begin() as db:
        db.add(
            PublishedObject(
                object_hash=identity,
                payload_bytes=payload,
                byte_length=len(payload) + 1,
                media_type=PACKAGE_MEDIA_TYPE,
                schema_id=SCHEMA_ID,
                schema_version=SCHEMA_VERSION,
                canonicalization="RFC8785",
            )
        )
    with SessionLocal() as db:
        _assert_code(
            "published_object_metadata_mismatch",
            lambda: load_verified_published_object(db, identity),
        )


@pytest.mark.parametrize(
    ("payload", "identity", "expected_code"),
    [
        (
            canonicalize_value({"actual": "bytes"}),
            "sha256:" + "a" * 64,
            "published_object_hash_mismatch",
        ),
        (
            b'{ "not": "canonical" }',
            "sha256:" + "b" * 64,
            "published_object_noncanonical",
        ),
    ],
)
def test_corrupt_prepared_row_fails_closed_on_export(
    payload, identity, expected_code
):
    with SessionLocal.begin() as db:
        db.add(
            PublishedObject(
                object_hash=identity,
                payload_bytes=payload,
                byte_length=len(payload),
                media_type=PACKAGE_MEDIA_TYPE,
                schema_id=SCHEMA_ID,
                schema_version=SCHEMA_VERSION,
                canonicalization="RFC8785",
            )
        )
    with SessionLocal() as db:
        _assert_code(
            expected_code, lambda: load_verified_published_object(db, identity)
        )


def test_missing_object_has_an_explicit_error():
    with SessionLocal() as db:
        _assert_code(
            "published_object_missing",
            lambda: load_verified_published_object(db, "sha256:" + "c" * 64),
        )


@pytest.mark.parametrize("operation", ["update", "delete"])
def test_published_object_is_immutable_at_database_boundary(operation: str):
    payload, identity = _canonical({"immutable": True})
    with SessionLocal() as db:
        _put(db, payload, identity)
        db.commit()
        statement = (
            sa.text(
                "UPDATE published_objects SET payload_bytes=:payload "
                "WHERE object_hash=:identity"
            )
            if operation == "update"
            else sa.text(
                "DELETE FROM published_objects WHERE object_hash=:identity"
            )
        )
        with pytest.raises(sa.exc.DBAPIError):
            db.execute(statement, {"identity": identity, "payload": b"{}"})
            db.flush()
        db.rollback()
    with SessionLocal() as db:
        assert load_verified_published_object(db, identity).payload_bytes == payload
