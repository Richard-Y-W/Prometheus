import json
from pathlib import Path

from app.main import app


ROOT = Path(__file__).parents[2]


def test_checked_in_openapi_matches_application():
    checked_in = json.loads(
        (ROOT / "docs" / "openapi-v1.json").read_text(encoding="utf-8")
    )
    assert checked_in == app.openapi()
