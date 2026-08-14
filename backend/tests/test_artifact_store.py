from __future__ import annotations

import hashlib
import os
from pathlib import Path
from types import SimpleNamespace

import pytest
import sqlalchemy as sa

from app import artifact_store
from app.artifact_store import ArtifactIngestionError, ingest_local_artifact
from app.config import settings
from app.database import SessionLocal, engine
from app.models_v2 import ArtifactObjectV2


FIXTURE_BYTES = b'{"fixture":"program-01a"}\n'


def _hash(payload: bytes) -> str:
    return f"sha256:{hashlib.sha256(payload).hexdigest()}"


@pytest.fixture
def db():
    with SessionLocal() as session:
        yield session
        session.rollback()


@pytest.fixture
def source_file(tmp_path: Path) -> Path:
    source = tmp_path / "source.json"
    source.write_bytes(FIXTURE_BYTES)
    return source


def _ingest(db, source: Path, **overrides):
    arguments = {
        "source_path": source,
        "allowed_root": source.parent,
        "expected_hash": _hash(source.read_bytes()),
        "media_type": "application/json",
    }
    arguments.update(overrides)
    return ingest_local_artifact(db, **arguments)


def _assert_error(code: str, operation) -> ArtifactIngestionError:
    with pytest.raises(ArtifactIngestionError) as captured:
        operation()
    assert captured.value.code == code
    return captured.value


def test_external_deletion_does_not_change_ingested_bytes(
    db, source_file: Path
):
    expected_hash = _hash(FIXTURE_BYTES)
    stored = ingest_local_artifact(
        db,
        source_path=source_file,
        allowed_root=source_file.parent,
        expected_hash=expected_hash,
        media_type="application/json",
    )
    db.commit()

    source_file.unlink()

    assert stored.payload_bytes == FIXTURE_BYTES
    assert stored.object_hash == expected_hash
    assert db.get(ArtifactObjectV2, expected_hash).payload_bytes == FIXTURE_BYTES


def test_missing_source_fails_without_creating_an_object(db, tmp_path: Path):
    source = tmp_path / "missing.json"
    _assert_error(
        "artifact_missing",
        lambda: ingest_local_artifact(
            db,
            source_path=source,
            allowed_root=tmp_path,
            expected_hash=_hash(FIXTURE_BYTES),
            media_type="application/json",
        ),
    )
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_unreadable_source_is_distinct_from_missing(
    db, source_file: Path, monkeypatch
):
    def reject_open(*_args, **_kwargs):
        raise PermissionError("fixture cannot be read")

    monkeypatch.setattr(artifact_store.os, "open", reject_open)
    _assert_error("artifact_unreadable", lambda: _ingest(db, source_file))
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_source_deleted_after_validation_but_before_open_is_missing(
    db, source_file: Path
):
    def delete_after_validation(stage: str) -> None:
        if stage == "after_path_validation":
            source_file.unlink()

    _assert_error(
        "artifact_missing",
        lambda: _ingest(db, source_file, stage_callback=delete_after_validation),
    )
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_expected_hash_mismatch_fails_without_creating_an_object(
    db, source_file: Path
):
    _assert_error(
        "artifact_hash_mismatch",
        lambda: _ingest(db, source_file, expected_hash="sha256:" + "0" * 64),
    )
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_mutation_during_read_is_rejected(db, source_file: Path):
    changed = False

    def mutate_after_chunk(_total_bytes: int) -> None:
        nonlocal changed
        if not changed:
            source_file.write_bytes(FIXTURE_BYTES + b"changed")
            changed = True

    _assert_error(
        "artifact_changed_during_ingestion",
        lambda: _ingest(db, source_file, chunk_callback=mutate_after_chunk),
    )
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_descriptor_identity_mismatch_is_rejected(db, source_file: Path):
    calls = 0

    def mismatching_fstat(descriptor: int):
        nonlocal calls
        calls += 1
        current = os.fstat(descriptor)
        return SimpleNamespace(
            st_dev=current.st_dev,
            st_ino=current.st_ino + (1 if calls > 1 else 0),
            st_mode=current.st_mode,
            st_size=current.st_size,
            st_mtime_ns=current.st_mtime_ns,
        )

    _assert_error(
        "artifact_changed_during_ingestion",
        lambda: _ingest(db, source_file, descriptor_stat=mismatching_fstat),
    )


def test_traversal_cannot_escape_allowed_root(db, tmp_path: Path):
    allowed_root = tmp_path / "allowed"
    allowed_root.mkdir()
    outside = tmp_path / "outside.json"
    outside.write_bytes(FIXTURE_BYTES)
    traversal = allowed_root / ".." / outside.name

    _assert_error(
        "artifact_path_escape",
        lambda: ingest_local_artifact(
            db,
            source_path=traversal,
            allowed_root=allowed_root,
            expected_hash=_hash(FIXTURE_BYTES),
            media_type="application/json",
        ),
    )


def test_symlink_inside_root_cannot_select_outside_file(db, tmp_path: Path):
    allowed_root = tmp_path / "allowed"
    allowed_root.mkdir()
    outside = tmp_path / "outside.json"
    outside.write_bytes(FIXTURE_BYTES)
    link = allowed_root / "source.json"
    try:
        link.symlink_to(outside)
    except OSError as exc:
        pytest.skip(f"symlink creation unavailable: {exc}")

    _assert_error(
        "artifact_symlink",
        lambda: ingest_local_artifact(
            db,
            source_path=link,
            allowed_root=allowed_root,
            expected_hash=_hash(FIXTURE_BYTES),
            media_type="application/json",
        ),
    )


def test_symlink_substitution_after_validation_is_rejected(
    db, source_file: Path, tmp_path: Path
):
    outside = tmp_path / "outside.json"
    outside.write_bytes(FIXTURE_BYTES)

    def substitute(stage: str) -> None:
        if stage == "after_path_validation":
            source_file.unlink()
            source_file.symlink_to(outside)

    _assert_error(
        "artifact_symlink",
        lambda: _ingest(db, source_file, stage_callback=substitute),
    )


def test_nfc_and_nfd_paths_remain_distinct_exact_inputs(db, tmp_path: Path):
    nfc = tmp_path / "caf\u00e9.json"
    nfd = tmp_path / "cafe\u0301.json"
    assert nfc.name != nfd.name
    nfc.write_bytes(b"nfc")
    nfd.write_bytes(b"nfd")

    if os.path.samefile(nfc, nfd):
        _assert_error(
            "artifact_hash_mismatch",
            lambda: _ingest(db, nfc, expected_hash=_hash(b"nfc")),
        )
        assert db.scalar(
            sa.select(sa.func.count()).select_from(ArtifactObjectV2)
        ) == 0
        return

    nfc_object = _ingest(db, nfc)
    nfd_object = _ingest(db, nfd)
    assert nfc_object.object_hash != nfd_object.object_hash
    assert nfc_object.original_filename == nfc.name
    assert nfd_object.original_filename == nfd.name
    assert nfc_object.payload_bytes == b"nfc"
    assert nfd_object.payload_bytes == b"nfd"


def test_input_over_eight_mib_is_rejected(db, tmp_path: Path):
    source = tmp_path / "too-large.bin"
    source.write_bytes(b"x" * (settings.max_artifact_bytes + 1))

    _assert_error("artifact_too_large", lambda: _ingest(db, source))
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 0


def test_exact_object_reingestion_reuses_one_row(db, source_file: Path):
    first = _ingest(db, source_file)
    second = _ingest(db, source_file)

    assert second is first
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 1


def test_same_hash_different_bytes_is_a_collision(
    db, source_file: Path, tmp_path: Path
):
    stored = _ingest(db, source_file)
    other = tmp_path / "other.json"
    other.write_bytes(b"different bytes")

    class ForcedHasher:
        def update(self, _chunk: bytes) -> None:
            return None

        def hexdigest(self) -> str:
            return stored.object_hash.removeprefix("sha256:")

    _assert_error(
        "artifact_hash_collision",
        lambda: _ingest(
            db,
            other,
            expected_hash=stored.object_hash,
            hasher_factory=ForcedHasher,
        ),
    )
    assert db.scalar(sa.select(sa.func.count()).select_from(ArtifactObjectV2)) == 1


@pytest.mark.parametrize("operation", ["update", "delete"])
def test_stored_artifact_is_immutable_at_database_boundary(
    db, source_file: Path, operation: str
):
    stored = _ingest(db, source_file)
    db.commit()
    statement = (
        sa.text(
            "UPDATE artifact_objects_v2 SET payload_bytes=:payload "
            "WHERE object_hash=:object_hash"
        )
        if operation == "update"
        else sa.text(
            "DELETE FROM artifact_objects_v2 WHERE object_hash=:object_hash"
        )
    )
    parameters = {"object_hash": stored.object_hash, "payload": b"changed"}

    with pytest.raises(sa.exc.DBAPIError):
        db.execute(statement, parameters)
        db.flush()
    db.rollback()

    with engine.connect() as connection:
        assert connection.execute(
            sa.text(
                "SELECT payload_bytes FROM artifact_objects_v2 "
                "WHERE object_hash=:object_hash"
            ),
            {"object_hash": stored.object_hash},
        ).scalar_one() == FIXTURE_BYTES
