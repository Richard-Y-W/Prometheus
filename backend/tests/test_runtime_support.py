from pathlib import Path

import tomllib


BACKEND_ROOT = Path(__file__).parents[1]


def test_declared_python_and_canonicalization_support():
    metadata = tomllib.loads(
        (BACKEND_ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]

    assert metadata["requires-python"] == ">=3.11,<3.15"
    assert "rfc8785==0.1.4" in metadata["dependencies"]
    assert "psycopg[binary]>=3.2,<4" in metadata["dependencies"]
