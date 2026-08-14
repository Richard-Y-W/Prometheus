"""Render the complete deterministic Program 01A v2 package vector."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
from typing import Any


BACKEND_ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(BACKEND_ROOT))

from app.canonical_json import canonicalize_value, object_hash  # noqa: E402
from app.contracts_v2 import (  # noqa: E402
    SCHEMA_ID,
    SCHEMA_VERSION,
    ExecutionComponentV2,
)


ROOT = BACKEND_ROOT.parent
SOURCE_FIXTURE = ROOT / "fixtures/evidence/pm-36-gm.synthetic.json"
OUTPUT_DIRECTORY = ROOT / "fixtures/contracts"
CAPABILITY_ID = "component_input.pm_36_gm"
REVISION_ID = "10000000-0000-4000-8000-000000000001"
COMPONENT_ID = "20000000-0000-4000-8000-000000000001"
EVIDENCE_ID = "50000000-0000-4000-8000-000000000001"


def _indexed_uuid(namespace: int, index: int) -> str:
    return f"{namespace}0000000-0000-4000-8000-{index:012x}"


def _sha256(source: bytes) -> str:
    return f"sha256:{hashlib.sha256(source).hexdigest()}"


def _claim_fingerprint(claim: dict[str, Any]) -> str:
    semantic: dict[str, Any] = {
        "revision_id": claim["revision_id"],
        "slot_id": claim["slot_id"],
        "value_state": claim["value_state"],
        "value": claim["value"],
        "provenance": claim["provenance"],
        "evidence_ids": claim["evidence_ids"],
        "validity_conditions": claim["validity_conditions"],
    }
    if claim["value_state"] == "known":
        semantic["unit"] = claim["unit"]
        semantic["original_value"] = claim["original_value"]
        semantic["original_unit"] = claim["original_unit"]
    return _sha256(canonicalize_value(semantic))


def _semantic_package() -> dict[str, Any]:
    source_bytes = SOURCE_FIXTURE.read_bytes()
    source = json.loads(source_bytes)
    artifact_hash = _sha256(source_bytes)
    parameters = sorted(source["parameters"], key=lambda item: item["name"])

    slots: list[dict[str, Any]] = []
    claims: list[dict[str, Any]] = []
    reviews: list[dict[str, Any]] = []
    slot_by_name: dict[str, str] = {}

    for index, parameter in enumerate(parameters, start=1):
        slot_id = _indexed_uuid(3, index)
        claim_id = _indexed_uuid(4, index)
        review_id = _indexed_uuid(6, index)
        is_unknown = parameter["value"]["kind"] == "unknown"
        slot_by_name[parameter["name"]] = slot_id
        slots.append(
            {
                "slot_id": slot_id,
                "name": parameter["name"],
                "quantity": parameter["quantity"],
                "dimension": parameter["dimension"],
                "required_for_execution": not is_unknown,
                "selected_claim_id": claim_id,
            }
        )
        claim: dict[str, Any] = {
            "claim_id": claim_id,
            "revision_id": REVISION_ID,
            "slot_id": slot_id,
            "value_state": "unknown" if is_unknown else "known",
            "value": parameter["value"],
            "validity_conditions": parameter["validity_conditions"],
            "provenance": "fixture_json_v2",
            "evidence_ids": [] if is_unknown else [EVIDENCE_ID],
        }
        if not is_unknown:
            claim.update(
                {
                    "unit": parameter["unit"],
                    "original_value": parameter["original_value"],
                    "original_unit": parameter["original_unit"],
                }
            )
        claim["claim_fingerprint"] = _claim_fingerprint(claim)
        claims.append(claim)
        reviews.append(
            {
                "review_event_id": review_id,
                "revision_id": REVISION_ID,
                "claim_id": claim_id,
                "reviewed_claim_fingerprint": claim["claim_fingerprint"],
                "decision": "accepted",
                "reviewed_by": "contract-fixture-reviewer",
                "note": (
                    "Accepted as synthetic conformance input; no physical "
                    "validity is implied."
                ),
                "reviewed_at": "2026-08-11T00:00:00Z",
                "applied_draft_version": 1,
            }
        )

    claim_ids = [claim["claim_id"] for claim in claims]
    review_ids = [review["review_event_id"] for review in reviews]
    source_limitation = source["limitations"][0]
    gates = [
        {
            "gate_id": _indexed_uuid(7, 1),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "component_identity",
            "state": "satisfied",
            "satisfying_reference_ids": [COMPONENT_ID],
            "reason": None,
        },
        {
            "gate_id": _indexed_uuid(7, 2),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "source_artifact",
            "state": "satisfied",
            "satisfying_reference_ids": [artifact_hash],
            "reason": None,
        },
        {
            "gate_id": _indexed_uuid(7, 3),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "claim_selection",
            "state": "satisfied",
            "satisfying_reference_ids": claim_ids,
            "reason": None,
        },
        {
            "gate_id": _indexed_uuid(7, 4),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "claim_review",
            "state": "satisfied",
            "satisfying_reference_ids": review_ids,
            "reason": None,
        },
        {
            "gate_id": _indexed_uuid(7, 5),
            "capability_id": CAPABILITY_ID,
            "phase": "execution",
            "required_review_type": "package_consumer",
            "state": "blocked",
            "satisfying_reference_ids": [],
            "reason": "Program 01A has no v2 package consumer or solver execution.",
        },
    ]

    return {
        "$schema": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "package_kind": "component_execution_input",
        "package_compiler": {"name": "prometheus_python", "version": "0.2.0"},
        "revision_id": REVISION_ID,
        "reviewed_draft_version": 1,
        "component": {
            "component_id": COMPONENT_ID,
            "manufacturer": source["manufacturer"],
            "part_number": source["part_number"],
            "revision": source["revision"],
            "component_class": source["component_class"],
        },
        "capability_id": CAPABILITY_ID,
        "artifacts": [
            {
                "artifact_hash": artifact_hash,
                "media_type": "application/json",
                "byte_length": len(source_bytes),
                "filename": SOURCE_FIXTURE.name,
                "artifact_role": "source_evidence",
            }
        ],
        "parameter_slots": slots,
        "claims": claims,
        "evidence": [
            {
                "evidence_id": EVIDENCE_ID,
                "revision_id": REVISION_ID,
                "evidence_class": "private_upload",
                "source_authority": "synthetic_fixture",
                "physical_validation_status": "unvalidated",
                "extraction_confidence": None,
                "source_uri": None,
                "source_locator": "parameters",
                "excerpt": "Synthetic PM-36-GM conformance parameter table.",
                "page": None,
                "limitations": [source_limitation],
                "artifact_hash": artifact_hash,
                "local_provenance": (
                    "Exact checked-in Prometheus synthetic fixture; not a "
                    "manufacturer publication."
                ),
            }
        ],
        "claim_reviews": reviews,
        "gates": gates,
        "execution_readiness": "blocked",
        "missing_information": [
            {
                "missing_information_id": _indexed_uuid(8, 1),
                "slot_id": slot_by_name["gearbox_lifetime"],
                "reason": "synthetic fixture does not define gearbox lifetime",
                "unlocks_capability_ids": [],
            }
        ],
        "limitations": [
            {"limitation_id": _indexed_uuid(9, 1), "statement": source_limitation},
            {
                "limitation_id": _indexed_uuid(9, 2),
                "statement": (
                    "Program 01A validates reviewed input identity; it does not "
                    "execute an engineering solver."
                ),
            },
        ],
        "authority": {
            "package_role": "reviewed_input",
            "engineering_decision_authority": "prometheus_cpp",
            "authority_role": "input_only",
        },
    }


def render_fixture_files() -> dict[str, bytes]:
    """Return the human, canonical, and hash files for the package vector."""

    package = ExecutionComponentV2.model_validate(_semantic_package()).model_dump(
        mode="json", by_alias=True
    )
    human = (
        json.dumps(package, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")
    canonical = canonicalize_value(package)
    identity = (object_hash(canonical) + "\n").encode("ascii")
    stem = "execution-component-v2.pm-36-gm"
    return {
        f"{stem}.json": human,
        f"{stem}.jcs": canonical,
        f"{stem}.sha256": identity,
    }


def main() -> int:
    for filename, payload in render_fixture_files().items():
        (OUTPUT_DIRECTORY / filename).write_bytes(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
