import json
import sys
from pathlib import Path


BACKEND_ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(BACKEND_ROOT))

from app.main import app  # noqa: E402


ROOT = BACKEND_ROOT.parent
OUTPUT = ROOT / "docs" / "openapi-v1.json"
OUTPUT.write_text(
    json.dumps(app.openapi(), indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print(OUTPUT)
