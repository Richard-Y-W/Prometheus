"""Render deterministic Program 01B consumer and Motor A/B package vectors."""

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
from app.execution_contracts_v1 import (  # noqa: E402
    APPLICABILITY_IDS,
    AVAILABLE_BUT_UNUSED_SLOT_SPECS,
    BACKEND_CONTRACT_VERSION,
    BACKEND_ID,
    CONSUMER_SCHEMA_ID,
    OBLIGATION_IDS,
    REQUIRED_SLOT_SPECS,
    SCENARIO_SCHEMA_ID,
    VALIDATION_SLOT_SPECS,
    PackageConsumerContractV1,
)


ROOT = BACKEND_ROOT.parent
OUTPUT_DIRECTORY = ROOT / "fixtures/contracts"
CAPABILITY_ID = "component_input.dc_gearmotor_v1"
CONSUMER_STEM = "package-consumer.motor-arm-builtin-v1"
CONSUMER_MEDIA_TYPE = (
    "application/vnd.prometheus.package-consumer-contract+json;version=1.0.0"
)
REQUIRED_FOR_EXECUTION = frozenset(
    spec[0] for spec in (*REQUIRED_SLOT_SPECS, *VALIDATION_SLOT_SPECS)
)
MOTOR_SOURCES = (
    (
        "motor-a",
        ROOT / "fixtures/evidence/motor-a.synthetic-v1.json",
        1,
    ),
    (
        "motor-b",
        ROOT / "fixtures/evidence/motor-b.synthetic-v1.json",
        2,
    ),
)


def _sha256(source: bytes) -> str:
    return f"sha256:{hashlib.sha256(source).hexdigest()}"


def _uuid(namespace: int, category: int, index: int) -> str:
    return f"{namespace:x}{category:x}000000-0000-4000-8000-{index:012x}"


def _consumer_slot(spec: tuple[str, str, str, str, str]) -> dict[str, str]:
    return {
        "slot_name": spec[0],
        "engineering_quantity": spec[1],
        "dimension": spec[2],
        "value_shape": spec[3],
        "canonical_unit": spec[4],
    }


def _consumer_semantic() -> dict[str, Any]:
    return {
        "$schema": CONSUMER_SCHEMA_ID,
        "schema_version": "1.0.0",
        "contract_kind": "package_consumer",
        "backend": {
            "backend_id": BACKEND_ID,
            "contract_version": BACKEND_CONTRACT_VERSION,
        },
        "accepted_package": {
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
            "package_kind": "component_execution_input",
            "capability": CAPABILITY_ID,
        },
        "required_slots": [_consumer_slot(spec) for spec in REQUIRED_SLOT_SPECS],
        "validation_slots": [
            _consumer_slot(spec) for spec in VALIDATION_SLOT_SPECS
        ],
        "available_but_unused_slots": [
            _consumer_slot(spec) for spec in AVAILABLE_BUT_UNUSED_SLOT_SPECS
        ],
        "supported_scenario": {
            "schema_id": SCENARIO_SCHEMA_ID,
            "schema_version": "1.0.0",
            "scenario_kind": "motor_arm",
            "motion_profiles": ["symmetric_triangular_velocity"],
        },
        "obligation_ids": list(OBLIGATION_IDS),
        "applicability_ids": list(APPLICABILITY_IDS),
        "validation_level": "synthetic_conformance_only",
    }


def _render_object(
    semantic: dict[str, Any], *, stem: str
) -> dict[str, bytes]:
    human = (
        json.dumps(semantic, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")
    canonical = canonicalize_value(semantic)
    identity = (object_hash(canonical) + "\n").encode("ascii")
    return {
        f"{stem}.json": human,
        f"{stem}.jcs": canonical,
        f"{stem}.sha256": identity,
    }


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
        semantic.update(
            {
                "unit": claim["unit"],
                "original_value": claim["original_value"],
                "original_unit": claim["original_unit"],
            }
        )
    return _sha256(canonicalize_value(semantic))


def _semantic_package(
    *,
    source_path: Path,
    namespace: int,
    consumer_bytes: bytes,
    consumer_hash: str,
) -> dict[str, Any]:
    source_bytes = source_path.read_bytes()
    source = json.loads(source_bytes)
    source_hash = _sha256(source_bytes)
    parameters = sorted(source["parameters"], key=lambda item: item["name"])
    revision_id = _uuid(namespace, 1, 1)
    component_id = _uuid(namespace, 2, 1)

    slots: list[dict[str, Any]] = []
    claims: list[dict[str, Any]] = []
    evidence: list[dict[str, Any]] = []
    reviews: list[dict[str, Any]] = []
    slot_by_name: dict[str, str] = {}

    for index, parameter in enumerate(parameters, start=1):
        slot_id = _uuid(namespace, 3, index)
        claim_id = _uuid(namespace, 4, index)
        review_id = _uuid(namespace, 6, index)
        is_unknown = "unknown_reason" in parameter
        slot_by_name[parameter["name"]] = slot_id
        slots.append(
            {
                "slot_id": slot_id,
                "name": parameter["name"],
                "quantity": parameter["quantity"],
                "dimension": parameter["dimension"],
                "required_for_execution": parameter["name"]
                in REQUIRED_FOR_EXECUTION,
                "selected_claim_id": claim_id,
            }
        )
        evidence_ids: list[str] = []
        if is_unknown:
            value = {"kind": "unknown", "reason": parameter["unknown_reason"]}
            value_state = "unknown"
        else:
            evidence_id = _uuid(namespace, 5, index)
            evidence_ids.append(evidence_id)
            value = parameter["value"]
            value_state = "known"
            evidence.append(
                {
                    "evidence_id": evidence_id,
                    "revision_id": revision_id,
                    "evidence_class": "private_upload",
                    "source_authority": "synthetic_fixture",
                    "physical_validation_status": "unvalidated",
                    "extraction_confidence": None,
                    "source_uri": None,
                    "source_locator": parameter["source_locator"],
                    "excerpt": (
                        f"{parameter['name']}: {parameter['original_value']} "
                        f"{parameter['original_unit']}"
                    ),
                    "page": None,
                    "limitations": list(source["limitations"]),
                    "artifact_hash": source_hash,
                    "local_provenance": (
                        "Exact checked-in Prometheus synthetic fixture; not a "
                        "manufacturer publication."
                    ),
                }
            )
        claim: dict[str, Any] = {
            "claim_id": claim_id,
            "revision_id": revision_id,
            "slot_id": slot_id,
            "value_state": value_state,
            "value": value,
            "validity_conditions": list(parameter["validity_conditions"]),
            "provenance": "fixture_json_v2",
            "evidence_ids": evidence_ids,
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
                "revision_id": revision_id,
                "claim_id": claim_id,
                "reviewed_claim_fingerprint": claim["claim_fingerprint"],
                "decision": "accepted",
                "reviewed_by": "program-01b-fixture-reviewer",
                "note": (
                    "Accepted as synthetic conformance input; no physical "
                    "validity is implied."
                ),
                "reviewed_at": "2026-08-12T00:00:00Z",
                "applied_draft_version": 1,
            }
        )

    claim_ids = [claim["claim_id"] for claim in claims]
    review_ids = [review["review_event_id"] for review in reviews]
    gates = [
        {
            "gate_id": _uuid(namespace, 7, 1),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "component_identity",
            "state": "satisfied",
            "satisfying_reference_ids": [component_id],
            "reason": None,
        },
        {
            "gate_id": _uuid(namespace, 7, 2),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "source_artifact",
            "state": "satisfied",
            "satisfying_reference_ids": [source_hash],
            "reason": None,
        },
        {
            "gate_id": _uuid(namespace, 7, 3),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "claim_selection",
            "state": "satisfied",
            "satisfying_reference_ids": claim_ids,
            "reason": None,
        },
        {
            "gate_id": _uuid(namespace, 7, 4),
            "capability_id": CAPABILITY_ID,
            "phase": "publication",
            "required_review_type": "claim_review",
            "state": "satisfied",
            "satisfying_reference_ids": review_ids,
            "reason": None,
        },
        {
            "gate_id": _uuid(namespace, 7, 5),
            "capability_id": CAPABILITY_ID,
            "phase": "execution",
            "required_review_type": "package_consumer",
            "state": "satisfied",
            "satisfying_reference_ids": [consumer_hash],
            "reason": None,
        },
    ]
    artifacts = sorted(
        [
            {
                "artifact_hash": source_hash,
                "media_type": "application/json",
                "byte_length": len(source_bytes),
                "filename": source_path.name,
                "artifact_role": "source_evidence",
            },
            {
                "artifact_hash": consumer_hash,
                "media_type": CONSUMER_MEDIA_TYPE,
                "byte_length": len(consumer_bytes),
                "filename": f"{CONSUMER_STEM}.jcs",
                "artifact_role": "supporting_input",
            },
        ],
        key=lambda artifact: artifact["artifact_hash"],
    )
    source_limitation = source["limitations"][0]
    semantic = {
        "$schema": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "package_kind": "component_execution_input",
        "package_compiler": {"name": "prometheus_python", "version": "0.2.0"},
        "revision_id": revision_id,
        "reviewed_draft_version": 1,
        "component": {
            "component_id": component_id,
            "manufacturer": source["manufacturer"],
            "part_number": source["part_number"],
            "revision": source["revision"],
            "component_class": source["component_class"],
        },
        "capability_id": CAPABILITY_ID,
        "artifacts": artifacts,
        "parameter_slots": slots,
        "claims": claims,
        "evidence": evidence,
        "claim_reviews": reviews,
        "gates": gates,
        "execution_readiness": "ready",
        "missing_information": [
            {
                "missing_information_id": _uuid(namespace, 8, 1),
                "slot_id": slot_by_name["gearbox_lifetime"],
                "reason": "synthetic fixture does not define gearbox lifetime",
                "unlocks_capability_ids": [],
            }
        ],
        "limitations": [
            {
                "limitation_id": _uuid(namespace, 9, 1),
                "statement": source_limitation,
            },
            {
                "limitation_id": _uuid(namespace, 9, 2),
                "statement": (
                    "The reviewed consumer accepts this synthetic conformance "
                    "package only; no physical validity is implied."
                ),
            },
        ],
        "authority": {
            "package_role": "reviewed_input",
            "engineering_decision_authority": "prometheus_cpp",
            "authority_role": "input_only",
        },
    }
    return ExecutionComponentV2.model_validate(semantic).model_dump(
        mode="json", by_alias=True
    )


def render_program_01b_files() -> dict[str, bytes]:
    """Return all exact consumer and Motor A/B fixture files."""

    consumer = PackageConsumerContractV1.model_validate(
        _consumer_semantic()
    ).model_dump(mode="json", by_alias=True)
    consumer_files = _render_object(consumer, stem=CONSUMER_STEM)
    consumer_bytes = consumer_files[f"{CONSUMER_STEM}.jcs"]
    consumer_hash = consumer_files[f"{CONSUMER_STEM}.sha256"].decode(
        "ascii"
    ).strip()
    rendered = dict(consumer_files)
    for stem, source_path, namespace in MOTOR_SOURCES:
        package = _semantic_package(
            source_path=source_path,
            namespace=namespace,
            consumer_bytes=consumer_bytes,
            consumer_hash=consumer_hash,
        )
        rendered.update(
            _render_object(package, stem=f"execution-component-v2.{stem}")
        )
    return rendered


def main() -> int:
    for filename, payload in render_program_01b_files().items():
        (OUTPUT_DIRECTORY / filename).write_bytes(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
