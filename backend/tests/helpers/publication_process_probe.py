"""Replay one publication from a fresh Python process and emit exact bytes."""

from __future__ import annotations

import argparse
import base64
import json
import os
from pathlib import Path
import sys


BACKEND_ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(BACKEND_ROOT))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database-url", required=True)
    parser.add_argument("--revision-id", required=True)
    parser.add_argument("--idempotency-key", required=True)
    parser.add_argument("--expected-draft-version", type=int, required=True)
    arguments = parser.parse_args()
    os.environ["PROMETHEUS_DATABASE_URL"] = arguments.database_url

    from app.contracts_v2 import (  # noqa: PLC0415
        PublicationRequestV2,
        SCHEMA_ID,
        SCHEMA_VERSION,
    )
    from app.database import SessionLocal  # noqa: PLC0415
    from app.publication_service_v2 import publish_revision  # noqa: PLC0415

    response = publish_revision(
        revision_id=arguments.revision_id,
        idempotency_key=arguments.idempotency_key,
        request=PublicationRequestV2(
            expected_draft_version=arguments.expected_draft_version,
            schema_id=SCHEMA_ID,
            schema_version=SCHEMA_VERSION,
        ),
        session_factory=SessionLocal,
    )
    print(
        json.dumps(
            {
                "status_code": response.status_code,
                "body_base64": base64.b64encode(response.body).decode("ascii"),
                "headers": response.headers,
                "object_hash": response.object_hash,
            },
            sort_keys=True,
            separators=(",", ":"),
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
