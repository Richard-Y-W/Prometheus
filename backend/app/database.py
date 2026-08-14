import sqlite3

from sqlalchemy import create_engine, event
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from .config import settings


class Base(DeclarativeBase):
    pass


MINIMUM_SQLITE_VERSION = (3, 35, 0)


def require_supported_sqlite(
    version: tuple[int, ...] = sqlite3.sqlite_version_info,
) -> None:
    if version < MINIMUM_SQLITE_VERSION:
        required = ".".join(str(part) for part in MINIMUM_SQLITE_VERSION)
        actual = ".".join(str(part) for part in version)
        raise RuntimeError(
            f"Prometheus requires SQLite {required} or newer; found {actual}"
        )


engine_options: dict[str, object] = {}
if settings.database_url.startswith("sqlite"):
    require_supported_sqlite()
    engine_options["connect_args"] = {"check_same_thread": False}
elif settings.database_url.startswith("postgresql"):
    engine_options["isolation_level"] = "READ COMMITTED"

engine = create_engine(settings.database_url, **engine_options)

if settings.database_url.startswith("sqlite"):

    @event.listens_for(engine, "connect")
    def enable_sqlite_foreign_keys(dbapi_connection, _connection_record):
        cursor = dbapi_connection.cursor()
        cursor.execute("PRAGMA foreign_keys=ON")
        cursor.execute("PRAGMA busy_timeout=5000")
        cursor.close()


SessionLocal = sessionmaker(engine, expire_on_commit=False)


def get_db():
    with SessionLocal() as db:
        yield db
