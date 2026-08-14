"""Dialect-correct revision write locks for v2 review and publication."""

from __future__ import annotations

from collections.abc import Callable, Iterator
from contextlib import contextmanager
from typing import Literal

import sqlalchemy as sa
from sqlalchemy.exc import DBAPIError
from sqlalchemy.orm import Session, sessionmaker

from .models_v1 import ComponentRevision


WriteOperation = Literal["review", "publication"]
MAX_POSTGRES_WRITE_ATTEMPTS = 3
_POSTGRES_RETRYABLE_STATES = {"40001", "40P01", "55P03"}
_SQLITE_BUSY_CODES = {5, 6}


class RetryableWriteError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class RevisionLockError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _retryable_lock_failure(error: DBAPIError) -> bool:
    original = error.orig
    sqlite_code = getattr(original, "sqlite_errorcode", None)
    if sqlite_code in _SQLITE_BUSY_CODES:
        return True
    sqlstate = getattr(original, "sqlstate", None) or getattr(
        original, "pgcode", None
    )
    return sqlstate in _POSTGRES_RETRYABLE_STATES


@contextmanager
def locked_revision_transaction(
    session_factory: sessionmaker,
    revision_id: str,
    *,
    operation: WriteOperation,
    stage_callback: Callable[[str], None] | None = None,
) -> Iterator[tuple[Session, ComponentRevision]]:
    """Lock one revision and commit exactly once if the body succeeds.

    PostgreSQL retryable SQLSTATEs are surfaced as ``RetryableWriteError`` so
    the operation service can rerun its complete deterministic body, at most
    ``MAX_POSTGRES_WRITE_ATTEMPTS`` times.  Replaying only the lock acquisition
    would be unsafe after a serialization failure during flush or commit.
    """

    if operation not in ("review", "publication"):
        raise ValueError(f"unsupported write operation {operation!r}")
    session = session_factory()
    try:
        dialect = session.get_bind().dialect.name
        if dialect == "sqlite":
            session.connection().exec_driver_sql("BEGIN IMMEDIATE")
            revision = session.get(ComponentRevision, revision_id)
        elif dialect == "postgresql":
            revision = session.scalar(
                sa.select(ComponentRevision)
                .where(ComponentRevision.id == revision_id)
                .with_for_update()
            )
        else:
            raise RevisionLockError(
                "unsupported_database",
                f"revision locking is not defined for database {dialect!r}",
            )
        if revision is None:
            raise RevisionLockError(
                "revision_not_found", f"revision {revision_id!r} does not exist"
            )
        if stage_callback is not None:
            stage_callback("revision_locked")
        yield session, revision
        session.commit()
    except DBAPIError as exc:
        session.rollback()
        if _retryable_lock_failure(exc):
            raise RetryableWriteError(
                f"{operation}_busy",
                f"{operation} could not acquire or retain the revision write lock",
            ) from exc
        raise
    except BaseException:
        session.rollback()
        raise
    finally:
        session.close()


__all__ = [
    "MAX_POSTGRES_WRITE_ATTEMPTS",
    "RetryableWriteError",
    "RevisionLockError",
    "locked_revision_transaction",
]
