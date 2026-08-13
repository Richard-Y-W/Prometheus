"""Deterministically export the Program 01A v2 JSON Schemas."""

from __future__ import annotations

import json
from pathlib import Path
import sys
from typing import Any

from pydantic import TypeAdapter


BACKEND_ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(BACKEND_ROOT))

from app.contracts_v2 import (
    EngineeringValueV2,
    EvidenceRecordV2,
    ExecutionComponentV2,
    ProjectSummaryV2,
    PublicationRequestV2,
    ReviewRequestV2,
)
from app.execution_contracts_v1 import (
    CONSUMER_SCHEMA_ID,
    MANIFEST_SCHEMA_ID,
    REQUEST_SCHEMA_ID,
    RESULT_SCHEMA_ID,
    SCENARIO_SCHEMA_ID,
    AnalysisRequestV1,
    AnalysisResultV1,
    MotorArmScenarioV1,
    PackageConsumerContractV1,
    RunManifestV1,
)


ROOT = BACKEND_ROOT.parent
DIALECT = "https://json-schema.org/draft/2020-12/schema"


def _schema(schema_id: str, adapter: TypeAdapter[Any]) -> dict[str, Any]:
    schema = adapter.json_schema(mode="validation")
    schema["$schema"] = DIALECT
    schema["$id"] = schema_id
    return schema


def _project_summary_conditions() -> list[dict[str, Any]]:
    return [
        {
            "if": {
                "properties": {
                    "counts": {
                        "properties": {"violated": {"minimum": 1}},
                        "required": ["violated"],
                    }
                },
                "required": ["counts"],
            },
            "then": {
                "properties": {"verdict": {"const": "requirements_violated"}}
            },
        },
        {
            "if": {
                "properties": {"verdict": {"const": "requirements_violated"}},
                "required": ["verdict"],
            },
            "then": {
                "properties": {
                    "counts": {
                        "properties": {"violated": {"minimum": 1}},
                        "required": ["violated"],
                    }
                }
            },
        },
        {
            "if": {
                "properties": {"verdict": {"const": "satisfied_within_scope"}},
                "required": ["verdict"],
            },
            "then": {
                "properties": {
                    "coverage": {"const": "sufficient"},
                    "execution_state": {"const": "completed"},
                    "counts": {
                        "properties": {
                            "satisfied_within_scope": {"minimum": 1},
                            "violated": {"const": 0},
                            "indeterminate": {"const": 0},
                            "not_evaluated": {"const": 0},
                        },
                        "required": [
                            "satisfied_within_scope",
                            "violated",
                            "indeterminate",
                            "not_evaluated",
                        ],
                    },
                }
            },
        },
        {
            "if": {
                "properties": {"coverage": {"const": "not_assessed"}},
                "required": ["coverage"],
            },
            "then": {
                "properties": {
                    "counts": {
                        "properties": {
                            "satisfied_within_scope": {"const": 0},
                            "violated": {"const": 0},
                            "indeterminate": {"const": 0},
                            "not_evaluated": {"const": 0},
                        },
                        "required": [
                            "satisfied_within_scope",
                            "violated",
                            "indeterminate",
                            "not_evaluated",
                        ],
                    }
                }
            },
        },
        {
            "if": {
                "properties": {"coverage": {"const": "insufficient"}},
                "required": ["coverage"],
            },
            "then": {
                "properties": {
                    "counts": {
                        "anyOf": [
                            {
                                "properties": {"indeterminate": {"minimum": 1}},
                                "required": ["indeterminate"],
                            },
                            {
                                "properties": {"not_evaluated": {"minimum": 1}},
                                "required": ["not_evaluated"],
                            },
                        ]
                    }
                }
            },
        },
        {
            "if": {
                "properties": {"coverage": {"const": "sufficient"}},
                "required": ["coverage"],
            },
            "then": {
                "properties": {
                    "counts": {
                        "properties": {
                            "indeterminate": {"const": 0},
                            "not_evaluated": {"const": 0},
                        },
                        "required": ["indeterminate", "not_evaluated"],
                        "anyOf": [
                            {
                                "properties": {
                                    "satisfied_within_scope": {"minimum": 1}
                                },
                                "required": ["satisfied_within_scope"],
                            },
                            {
                                "properties": {"violated": {"minimum": 1}},
                                "required": ["violated"],
                            },
                        ],
                    }
                }
            },
        },
    ]


def render_schemas() -> dict[str, bytes]:
    """Return filename -> UTF-8 JSON Schema bytes with sorted keys and final newline."""

    adapters: dict[str, tuple[str, TypeAdapter[Any]]] = {
        "engineering-value-v2.schema.json": (
            "urn:prometheus:schema:engineering-value:2.0.0",
            TypeAdapter(EngineeringValueV2),
        ),
        "evidence-record-v2.schema.json": (
            "urn:prometheus:schema:evidence-record:2.0.0",
            TypeAdapter(EvidenceRecordV2),
        ),
        "review-request-v2.schema.json": (
            "urn:prometheus:schema:review-request:2.0.0",
            TypeAdapter(ReviewRequestV2),
        ),
        "publication-request-v2.schema.json": (
            "urn:prometheus:schema:publication-request:2.0.0",
            TypeAdapter(PublicationRequestV2),
        ),
        "execution-component-v2.schema.json": (
            "urn:prometheus:schema:execution-component:2.0.0",
            TypeAdapter(ExecutionComponentV2),
        ),
        "project-summary-v2.schema.json": (
            "urn:prometheus:schema:project-summary:2.0.0",
            TypeAdapter(ProjectSummaryV2),
        ),
        "package-consumer-contract-v1.schema.json": (
            CONSUMER_SCHEMA_ID,
            TypeAdapter(PackageConsumerContractV1),
        ),
        "motor-arm-scenario-v1.schema.json": (
            SCENARIO_SCHEMA_ID,
            TypeAdapter(MotorArmScenarioV1),
        ),
        "analysis-request-v1.schema.json": (
            REQUEST_SCHEMA_ID,
            TypeAdapter(AnalysisRequestV1),
        ),
        "analysis-result-v1.schema.json": (
            RESULT_SCHEMA_ID,
            TypeAdapter(AnalysisResultV1),
        ),
        "run-manifest-v1.schema.json": (
            MANIFEST_SCHEMA_ID,
            TypeAdapter(RunManifestV1),
        ),
    }
    rendered: dict[str, bytes] = {}
    for filename, (schema_id, adapter) in adapters.items():
        schema = _schema(schema_id, adapter)
        if filename == "project-summary-v2.schema.json":
            schema["allOf"] = _project_summary_conditions()
        rendered[filename] = (
            json.dumps(schema, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")
    return rendered


def main() -> int:
    for filename, payload in render_schemas().items():
        (ROOT / "schemas" / filename).write_bytes(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
