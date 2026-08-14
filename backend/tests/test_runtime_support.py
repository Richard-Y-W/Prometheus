import os
from pathlib import Path

import tomllib
import pytest

from app.database import MINIMUM_SQLITE_VERSION, engine, require_supported_sqlite


BACKEND_ROOT = Path(__file__).parents[1]


def test_declared_python_and_canonicalization_support():
    metadata = tomllib.loads(
        (BACKEND_ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]

    assert metadata["requires-python"] == ">=3.11,<3.15"
    assert "rfc8785==0.1.4" in metadata["dependencies"]
    assert "psycopg[binary]>=3.2,<4" in metadata["dependencies"]


def test_configured_postgresql_suite_uses_the_postgresql_application_engine():
    if os.getenv("PROMETHEUS_TEST_POSTGRES_URL"):
        assert engine.dialect.name == "postgresql"
        with engine.connect() as connection:
            assert connection.get_isolation_level() == "READ COMMITTED"


def test_sqlite_335_is_the_minimum_supported_write_runtime():
    require_supported_sqlite(MINIMUM_SQLITE_VERSION)
    with pytest.raises(RuntimeError, match="requires SQLite 3.35.0 or newer"):
        require_supported_sqlite((3, 34, 9))
