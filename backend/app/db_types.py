"""Dialect-safe storage types used by the v2 trust boundary."""

from __future__ import annotations

from datetime import datetime, timezone

from sqlalchemy import DateTime, LargeBinary, String
from sqlalchemy.engine import Dialect
from sqlalchemy.types import TypeDecorator


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _as_utc(value: datetime | str) -> datetime:
    if isinstance(value, str):
        text = value
        try:
            parsed = datetime.fromisoformat(
                text[:-1] + "+00:00" if text.endswith("Z") else text
            )
        except ValueError as exc:
            raise ValueError("timestamp must be valid RFC 3339") from exc
    elif isinstance(value, datetime):
        parsed = value
    else:
        raise TypeError("timestamp must be a datetime or RFC 3339 string")
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ValueError("naive timestamps are not permitted")
    return parsed.astimezone(timezone.utc)


class UtcTimestamp(TypeDecorator[datetime]):
    """TIMESTAMPTZ on PostgreSQL and canonical RFC 3339 text on SQLite."""

    impl = String
    cache_ok = True

    def load_dialect_impl(self, dialect: Dialect):
        if dialect.name == "postgresql":
            return dialect.type_descriptor(DateTime(timezone=True))
        return dialect.type_descriptor(String(32))

    def process_bind_param(self, value, dialect: Dialect):
        if value is None:
            return None
        instant = _as_utc(value)
        if dialect.name == "postgresql":
            return instant
        return instant.isoformat(timespec="microseconds").replace("+00:00", "Z")

    def process_result_value(self, value, dialect: Dialect):
        del dialect
        if value is None:
            return None
        return _as_utc(value)


class ImmutableBytes(TypeDecorator[bytes]):
    """A byte-only BLOB/BYTEA value; mutability is enforced by table triggers."""

    impl = LargeBinary
    cache_ok = True

    def process_bind_param(self, value, dialect: Dialect):
        del dialect
        if value is None:
            return None
        if type(value) is not bytes:
            raise TypeError("immutable payload must be bytes")
        return value

    def process_result_value(self, value, dialect: Dialect):
        del dialect
        if value is None:
            return None
        return bytes(value)


__all__ = ["ImmutableBytes", "UtcTimestamp", "utc_now"]
