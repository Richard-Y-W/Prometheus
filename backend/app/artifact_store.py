"""Bounded, descriptor-based ingestion for immutable local source artifacts."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import errno
import hashlib
import os
from pathlib import Path
import re
import stat
from typing import Protocol

from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from .config import settings
from .models_v2 import ArtifactObjectV2


_HASH_PATTERN = re.compile(r"sha256:[0-9a-f]{64}\Z")
_READ_CHUNK_BYTES = 64 * 1024


class _IncrementalHasher(Protocol):
    def update(self, chunk: bytes) -> object: ...

    def hexdigest(self) -> str: ...


class ArtifactIngestionError(ValueError):
    """Stable, fail-closed local artifact ingestion error."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class _FileIdentity:
    device: int
    inode: int
    mode: int
    size: int
    modified_ns: int


def _identity(metadata) -> _FileIdentity:
    return _FileIdentity(
        device=metadata.st_dev,
        inode=metadata.st_ino,
        mode=metadata.st_mode,
        size=metadata.st_size,
        modified_ns=metadata.st_mtime_ns,
    )


def _error(code: str, message: str, cause: BaseException | None = None):
    error = ArtifactIngestionError(code, message)
    if cause is None:
        raise error
    raise error from cause


def _validated_paths(source_path: Path, allowed_root: Path) -> tuple[Path, os.stat_result]:
    try:
        root_metadata = allowed_root.lstat()
    except FileNotFoundError as exc:
        _error("artifact_path_escape", "allowed artifact root does not exist", exc)
    except OSError as exc:
        _error("artifact_unreadable", "allowed artifact root is unreadable", exc)
    if stat.S_ISLNK(root_metadata.st_mode):
        _error("artifact_symlink", "allowed artifact root must not be a symlink")
    if not stat.S_ISDIR(root_metadata.st_mode):
        _error("artifact_path_escape", "allowed artifact root is not a directory")

    root = allowed_root.resolve(strict=True)
    candidate = Path(os.path.abspath(os.fspath(source_path)))
    try:
        relative = candidate.relative_to(root)
    except ValueError:
        _error("artifact_path_escape", "artifact path escapes the allowed root")
    if not relative.parts:
        _error("artifact_unreadable", "artifact path names a directory")

    cursor = root
    for component in relative.parts[:-1]:
        cursor /= component
        try:
            component_metadata = cursor.lstat()
        except FileNotFoundError as exc:
            _error("artifact_missing", "artifact path does not exist", exc)
        except OSError as exc:
            _error("artifact_unreadable", "artifact path is unreadable", exc)
        if stat.S_ISLNK(component_metadata.st_mode):
            _error("artifact_symlink", "artifact path contains a symlink")
        if not stat.S_ISDIR(component_metadata.st_mode):
            _error("artifact_unreadable", "artifact parent is not a directory")

    try:
        source_metadata = candidate.lstat()
    except FileNotFoundError as exc:
        _error("artifact_missing", "artifact source does not exist", exc)
    except OSError as exc:
        _error("artifact_unreadable", "artifact source is unreadable", exc)
    if stat.S_ISLNK(source_metadata.st_mode):
        _error("artifact_symlink", "artifact source must not be a symlink")
    if not stat.S_ISREG(source_metadata.st_mode):
        _error("artifact_unreadable", "artifact source is not a regular file")
    return candidate, source_metadata


def _open_read_only(source: Path) -> int:
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        return os.open(source, flags)
    except FileNotFoundError as exc:
        _error("artifact_missing", "artifact disappeared before it could be opened", exc)
    except PermissionError as exc:
        _error("artifact_unreadable", "artifact source cannot be read", exc)
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            _error("artifact_symlink", "artifact became a symlink before open", exc)
        _error("artifact_unreadable", "artifact source cannot be opened", exc)


def _require_unchanged(
    expected: _FileIdentity, actual_metadata, *, message: str
) -> None:
    actual = _identity(actual_metadata)
    if actual != expected:
        _error("artifact_changed_during_ingestion", message)


def _read_descriptor(
    descriptor: int,
    *,
    maximum_bytes: int,
    hasher: _IncrementalHasher,
    chunk_callback: Callable[[int], None] | None,
) -> bytes:
    payload = bytearray()
    while True:
        try:
            chunk = os.read(descriptor, _READ_CHUNK_BYTES)
        except OSError as exc:
            _error("artifact_unreadable", "artifact read failed", exc)
        if not chunk:
            break
        payload.extend(chunk)
        if len(payload) > maximum_bytes:
            _error(
                "artifact_too_large",
                f"artifact exceeds the {maximum_bytes}-byte ingestion limit",
            )
        hasher.update(chunk)
        if chunk_callback is not None:
            chunk_callback(len(payload))
    return bytes(payload)


def _require_exact_existing(
    existing: ArtifactObjectV2,
    *,
    payload: bytes,
    media_type: str,
) -> ArtifactObjectV2:
    if (
        existing.payload_bytes != payload
        or existing.byte_length != len(payload)
        or existing.media_type != media_type
    ):
        _error(
            "artifact_hash_collision",
            "an artifact hash already identifies different bytes or metadata",
        )
    return existing


def ingest_local_artifact(
    db: Session,
    *,
    source_path: str | os.PathLike[str],
    allowed_root: str | os.PathLike[str],
    expected_hash: str,
    media_type: str,
    chunk_callback: Callable[[int], None] | None = None,
    stage_callback: Callable[[str], None] | None = None,
    hasher_factory: Callable[[], _IncrementalHasher] = hashlib.sha256,
    descriptor_stat: Callable[[int], object] = os.fstat,
) -> ArtifactObjectV2:
    """Copy, verify, and install one local file as an immutable artifact.

    The callbacks and dependency seams are deterministic race/collision probes
    used by the conformance suite.  Production callers leave them unset.
    """

    if not isinstance(expected_hash, str) or not _HASH_PATTERN.fullmatch(
        expected_hash
    ):
        _error("artifact_hash_mismatch", "expected artifact hash is malformed")
    if not isinstance(media_type, str) or not media_type.strip():
        _error("artifact_unreadable", "artifact media type must be non-empty")
    maximum_bytes = settings.max_artifact_bytes
    if type(maximum_bytes) is not int or maximum_bytes <= 0:
        _error("artifact_unreadable", "artifact byte limit is invalid")

    source, path_metadata = _validated_paths(Path(source_path), Path(allowed_root))
    if stage_callback is not None:
        stage_callback("after_path_validation")

    try:
        pre_open_metadata = source.lstat()
    except FileNotFoundError as exc:
        _error("artifact_missing", "artifact disappeared before it could be opened", exc)
    except OSError as exc:
        _error("artifact_unreadable", "artifact source cannot be inspected", exc)
    if stat.S_ISLNK(pre_open_metadata.st_mode):
        _error("artifact_symlink", "artifact became a symlink before open")
    _require_unchanged(
        _identity(path_metadata),
        pre_open_metadata,
        message="artifact changed between validation and descriptor open",
    )

    descriptor = _open_read_only(source)
    try:
        try:
            before_metadata = descriptor_stat(descriptor)
        except OSError as exc:
            _error("artifact_unreadable", "artifact descriptor cannot be inspected", exc)
        before = _identity(before_metadata)
        _require_unchanged(
            _identity(path_metadata),
            before_metadata,
            message="artifact changed between validation and descriptor open",
        )
        if not stat.S_ISREG(before.mode):
            _error("artifact_unreadable", "opened artifact is not a regular file")
        if before.size > maximum_bytes:
            _error(
                "artifact_too_large",
                f"artifact exceeds the {maximum_bytes}-byte ingestion limit",
            )
        if stage_callback is not None:
            stage_callback("after_descriptor_open")

        hasher = hasher_factory()
        payload = _read_descriptor(
            descriptor,
            maximum_bytes=maximum_bytes,
            hasher=hasher,
            chunk_callback=chunk_callback,
        )
        try:
            after_metadata = descriptor_stat(descriptor)
        except OSError as exc:
            _error(
                "artifact_changed_during_ingestion",
                "artifact descriptor disappeared during ingestion",
                exc,
            )
        _require_unchanged(
            before,
            after_metadata,
            message="artifact descriptor identity changed during ingestion",
        )
        try:
            final_path_metadata = source.lstat()
        except OSError as exc:
            _error(
                "artifact_changed_during_ingestion",
                "artifact path changed during ingestion",
                exc,
            )
        _require_unchanged(
            before,
            final_path_metadata,
            message="artifact path identity changed during ingestion",
        )
    finally:
        os.close(descriptor)

    if stage_callback is not None:
        stage_callback("after_copy")
    computed_hash = f"sha256:{hasher.hexdigest()}"
    if not _HASH_PATTERN.fullmatch(computed_hash) or computed_hash != expected_hash:
        _error(
            "artifact_hash_mismatch",
            f"copied artifact hash {computed_hash!r} does not match the expected hash",
        )

    existing = db.get(ArtifactObjectV2, computed_hash)
    if existing is not None:
        return _require_exact_existing(
            existing, payload=payload, media_type=media_type
        )

    artifact = ArtifactObjectV2(
        object_hash=computed_hash,
        payload_bytes=payload,
        byte_length=len(payload),
        media_type=media_type,
        origin_path=os.fspath(source),
        original_filename=source.name,
    )
    try:
        with db.begin_nested():
            db.add(artifact)
            db.flush()
    except IntegrityError:
        existing = db.get(ArtifactObjectV2, computed_hash)
        if existing is None:
            raise
        return _require_exact_existing(
            existing, payload=payload, media_type=media_type
        )
    return artifact


__all__ = ["ArtifactIngestionError", "ingest_local_artifact"]
