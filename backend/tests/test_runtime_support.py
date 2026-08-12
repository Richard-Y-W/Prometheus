import os
from pathlib import Path

import tomllib

from app.database import engine


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
