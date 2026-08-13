from __future__ import annotations

from copy import deepcopy
import json
import math
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator
from pydantic import ValidationError

from app.execution_contracts_v1 import (
    APPLICABILITY_IDS,
    BACKEND_CONTRACT_VERSION,
    BACKEND_ID,
    CALCULATION_IDS,
    CONSUMER_MEDIA_TYPE,
    CONSUMER_SCHEMA_ID,
    MANIFEST_MEDIA_TYPE,
    MANIFEST_SCHEMA_ID,
    OBLIGATION_IDS,
    REQUEST_MEDIA_TYPE,
    REQUEST_SCHEMA_ID,
    RESULT_MEDIA_TYPE,
    RESULT_SCHEMA_ID,
    SCENARIO_MEDIA_TYPE,
    SCENARIO_SCHEMA_ID,
    AnalysisRequestV1,
    AnalysisResultV1,
    MotorArmScenarioV1,
    PackageConsumerContractV1,
    RunManifestV1,
    StoredObjectReferenceV1,
)


ROOT = Path(__file__).parents[2]
APP_ROOT = ROOT / "backend/app"
HASHES = {letter: f"sha256:{letter * 64}" for letter in "abcdef"}


def _uuid(number: int) -> str:
    return f"10000000-0000-4000-8000-{number:012x}"


def _slot(
    name: str,
    quantity: str,
    dimension: str,
    value_shape: str,
    unit: str,
) -> dict[str, object]:
    return {
        "slot_name": name,
        "engineering_quantity": quantity,
        "dimension": dimension,
        "value_shape": value_shape,
        "canonical_unit": unit,
    }


def _consumer_payload() -> dict[str, object]:
    return {
        "$schema": CONSUMER_SCHEMA_ID,
        "schema_version": "1.0.0",
        "contract_kind": "package_consumer",
        "backend": {
            "backend_id": BACKEND_ID,
            "contract_version": BACKEND_CONTRACT_VERSION,
        },
        "accepted_package": {
            "schema_id": "urn:prometheus:schema:execution-component:2.0.0",
            "schema_version": "2.0.0",
            "package_kind": "component_execution_input",
            "capability": "component_input.dc_gearmotor_v1",
        },
        "required_slots": [
            _slot("gear_ratio", "ratio", "dimensionless", "scalar", "1"),
            _slot(
                "gearbox_efficiency_nominal",
                "efficiency",
                "dimensionless",
                "scalar",
                "1",
            ),
            _slot("continuous_torque_nm", "torque", "torque", "scalar", "N*m"),
            _slot("stall_torque_nm", "torque", "torque", "scalar", "N*m"),
            _slot(
                "no_load_speed_rad_s",
                "angular_velocity",
                "angle/time",
                "scalar",
                "rad/s",
            ),
            _slot(
                "no_load_current_a",
                "electric_current",
                "electric_current",
                "scalar",
                "A",
            ),
            _slot(
                "torque_constant_nm_a",
                "torque_constant",
                "torque/electric_current",
                "scalar",
                "N*m/A",
            ),
            _slot(
                "driver_current_limit_a",
                "electric_current_limit",
                "electric_current",
                "scalar",
                "A",
            ),
            _slot(
                "winding_resistance_ohm",
                "electrical_resistance",
                "electric_resistance",
                "scalar",
                "ohm",
            ),
            _slot(
                "thermal_resistance_k_w",
                "thermal_resistance",
                "temperature/power",
                "scalar",
                "K/W",
            ),
            _slot(
                "thermal_capacitance_j_k",
                "heat_capacity",
                "energy/temperature",
                "scalar",
                "J/K",
            ),
            _slot(
                "maximum_temperature_c",
                "temperature_limit",
                "temperature",
                "scalar",
                "degC",
            ),
        ],
        "validation_slots": [
            _slot(
                "gearbox_efficiency_range",
                "efficiency",
                "dimensionless",
                "range",
                "1",
            ),
            _slot(
                "torque_speed_curve",
                "torque_by_angular_velocity",
                "torque",
                "curve",
                "N*m",
            ),
        ],
        "available_but_unused_slots": [
            _slot(
                "nominal_voltage_v",
                "voltage",
                "electric_potential",
                "scalar",
                "V",
            ),
            _slot(
                "supply_current_limit_a",
                "electric_current_limit",
                "electric_current",
                "scalar",
                "A",
            ),
            _slot(
                "gearbox_lifetime",
                "service_life",
                "time",
                "scalar_or_unknown",
                "h",
            ),
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


def _scenario_payload() -> dict[str, object]:
    return {
        "$schema": SCENARIO_SCHEMA_ID,
        "schema_version": "1.0.0",
        "scenario_kind": "motor_arm",
        "payload_mass": {"value": 8, "unit": "kg"},
        "arm_radius": {"value": 0.2, "unit": "m"},
        "rotation": {"value": 1.5707963267948966, "unit": "rad"},
        "move_duration": {"value": 1.2, "unit": "s"},
        "hold_duration": {"value": 4, "unit": "s"},
        "cycle_duration": {"value": 10, "unit": "s"},
        "ambient_temperature": {"value": 35, "unit": "degC"},
        "motion_profile": "symmetric_triangular_velocity",
        "review": {
            "confirmed_by_user": True,
            "intent": (
                "Evaluate the bound motor for the reviewed motor-arm operating cycle."
            ),
        },
    }


def _request_payload() -> dict[str, object]:
    return {
        "$schema": REQUEST_SCHEMA_ID,
        "schema_version": "1.0.0",
        "request_kind": "motor_arm_analysis",
        "package_hash": HASHES["a"],
        "scenario_hash": HASHES["b"],
        "assembly_artifact_hash": HASHES["c"],
        "bound_cad_entity_id": "motor",
        "backend_id": BACKEND_ID,
        "backend_contract_version": BACKEND_CONTRACT_VERSION,
        "package_consumer_contract_hash": HASHES["d"],
        "obligation_ids": list(OBLIGATION_IDS),
    }


def _numeric_profile() -> dict[str, object]:
    return {
        "operating_system": {
            "name": "macos",
            "release": "26.0",
            "architecture": "arm64",
        },
        "compiler": {"id": "AppleClang", "version": "18.0.0"},
        "standard_library": {"id": "libc++", "version": "180000"},
        "math_runtime": {"id": "apple-libSystem", "version": "1504.100.15"},
        "backend_build_fingerprint": HASHES["e"],
        "floating_point": {
            "contraction": "disabled",
            "fast_math": False,
            "rounding_mode": "to_nearest",
        },
        "numeric_serialization_version": "1.0.0",
    }


def _input_bindings() -> dict[str, list[dict[str, object]]]:
    consumer = _consumer_payload()
    bindings: dict[str, list[dict[str, object]]] = {}
    groups = (
        ("calculation_inputs", "required_slots", "calculation_input", 1),
        ("validation_inputs", "validation_slots", "validation_input", 101),
        (
            "available_but_unused",
            "available_but_unused_slots",
            "available_but_unused",
            201,
        ),
    )
    for output_name, source_name, use, start in groups:
        bindings[output_name] = [
            {
                "slot_name": slot["slot_name"],
                "claim_id": _uuid(start + index),
                "input_use": use,
            }
            for index, slot in enumerate(consumer[source_name])
        ]
    return bindings


def _result_payload() -> dict[str, object]:
    consumed_inputs = _input_bindings()
    calculation_claims = [
        item["claim_id"] for item in consumed_inputs["calculation_inputs"]
    ]
    units = ("N*m", "N*m", "N*m", "N*m", "rad/s", "N*m", "A", "degC")
    outcome_units = ("N*m", "N*m", "A", "degC")
    outcomes = []
    for index, (obligation_id, unit) in enumerate(
        zip(OBLIGATION_IDS, outcome_units), start=1
    ):
        outcomes.append(
            {
                "finding_id": f"sha256:{index:064x}",
                "obligation_id": obligation_id,
                "outcome": "fail" if index == 2 else "pass",
                "severity": "error" if index == 2 else "info",
                "title": f"Scoped motor obligation {index}",
                "mechanism": "Inclusive comparison within the reviewed model scope.",
                "calculated_quantity": {"value": index + 0.25, "unit": unit},
                "comparison_quantity": {"value": index, "unit": unit},
                "comparison_operator": ">=",
                "signed_margin": -0.1 if index == 2 else 0.1,
                "package_hash": HASHES["a"],
                "request_hash": HASHES["f"],
                "scenario_hash": HASHES["b"],
                "consumed_claim_ids": calculation_claims[:2],
                "assumptions": ["The declared applicability conditions hold."],
                "limitations": ["Synthetic conformance evidence only."],
            }
        )
    return {
        "$schema": RESULT_SCHEMA_ID,
        "schema_version": "1.0.0",
        "execution_disposition": "completed",
        "request_hash": HASHES["f"],
        "package_hash": HASHES["a"],
        "backend": {
            "backend_id": BACKEND_ID,
            "contract_version": BACKEND_CONTRACT_VERSION,
            "numeric_profile": _numeric_profile(),
        },
        "calculations": [
            {"calculation_id": calculation_id, "value": index + 0.5, "unit": unit}
            for index, (calculation_id, unit) in enumerate(
                zip(CALCULATION_IDS, units), start=1
            )
        ],
        "consumed_inputs": consumed_inputs,
        "sensitivities": [
            {
                "sensitivity_id": "gearbox_efficiency_range",
                "claim_id": consumed_inputs["validation_inputs"][0]["claim_id"],
                "minimum_efficiency": 0.55,
                "maximum_efficiency": 0.82,
                "hold_margin_at_minimum": -0.2,
                "hold_margin_at_maximum": 0.1,
                "crosses_zero": True,
            }
        ],
        "obligation_outcomes": outcomes,
        "missing_information": [
            {
                "question_id": "assembly.center_of_gravity",
                "reason": "The model has no part-mass distribution.",
            }
        ],
        "assumptions": ["The payload is represented as a point mass."],
        "limitations": ["Synthetic conformance evidence only."],
        "applicability": list(APPLICABILITY_IDS),
        "coverage": {
            "requested_obligations": 4,
            "evaluated_obligations": 4,
            "counts": {
                "pass": 3,
                "fail": 1,
                "indeterminate": 0,
                "not_evaluated": 0,
            },
            "known_uncovered_questions": [
                {
                    "question_id": "assembly.center_of_gravity",
                    "reason": "Point-payload model has no part-mass distribution.",
                }
            ],
        },
    }


def _reference(
    object_hash: str, media_type: str, schema_id: str, schema_version: str
) -> dict[str, object]:
    return {
        "object_hash": object_hash,
        "byte_length": 512,
        "media_type": media_type,
        "schema_id": schema_id,
        "schema_version": schema_version,
    }


def _manifest_payload() -> dict[str, object]:
    return {
        "$schema": MANIFEST_SCHEMA_ID,
        "schema_version": "1.0.0",
        "manifest_kind": "completed_analysis_run",
        "package": _reference(
            HASHES["a"],
            "application/vnd.prometheus.execution-component+json;version=2.0.0",
            "urn:prometheus:schema:execution-component:2.0.0",
            "2.0.0",
        ),
        "scenario": _reference(
            HASHES["b"], SCENARIO_MEDIA_TYPE, SCENARIO_SCHEMA_ID, "1.0.0"
        ),
        "request": _reference(
            HASHES["f"], REQUEST_MEDIA_TYPE, REQUEST_SCHEMA_ID, "1.0.0"
        ),
        "result": _reference(
            HASHES["c"], RESULT_MEDIA_TYPE, RESULT_SCHEMA_ID, "1.0.0"
        ),
        "assembly_artifact_hash": HASHES["c"],
        "backend_id": BACKEND_ID,
        "backend_contract_version": BACKEND_CONTRACT_VERSION,
        "package_consumer_contract_hash": HASHES["d"],
        "numeric_profile": _numeric_profile(),
    }


def test_contract_constants_and_exact_media_types_are_frozen():
    assert CONSUMER_SCHEMA_ID == (
        "urn:prometheus:schema:package-consumer-contract:1.0.0"
    )
    assert SCENARIO_SCHEMA_ID == "urn:prometheus:schema:motor-arm-scenario:1.0.0"
    assert REQUEST_SCHEMA_ID == "urn:prometheus:schema:analysis-request:1.0.0"
    assert RESULT_SCHEMA_ID == "urn:prometheus:schema:analysis-result:1.0.0"
    assert MANIFEST_SCHEMA_ID == "urn:prometheus:schema:run-manifest:1.0.0"
    assert CONSUMER_MEDIA_TYPE == (
        "application/vnd.prometheus.package-consumer-contract+json;version=1.0.0"
    )
    assert SCENARIO_MEDIA_TYPE == (
        "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0"
    )
    assert REQUEST_MEDIA_TYPE == (
        "application/vnd.prometheus.analysis-request+json;version=1.0.0"
    )
    assert RESULT_MEDIA_TYPE == (
        "application/vnd.prometheus.analysis-result+json;version=1.0.0"
    )
    assert MANIFEST_MEDIA_TYPE == (
        "application/vnd.prometheus.run-manifest+json;version=1.0.0"
    )
    assert BACKEND_ID == "motor_arm_builtin_v1"
    assert BACKEND_CONTRACT_VERSION == "1.0.0"
    assert OBLIGATION_IDS == (
        "motor_arm.move_torque_speed",
        "motor_arm.hold_continuous_torque",
        "motor_arm.driver_current_limit",
        "motor_arm.thermal_peak",
    )


def test_transport_models_are_closed_and_do_not_add_volatile_or_global_fields():
    models = (
        PackageConsumerContractV1,
        MotorArmScenarioV1,
        AnalysisRequestV1,
        AnalysisResultV1,
        RunManifestV1,
    )
    forbidden = {
        "timestamp",
        "created_at",
        "updated_at",
        "run_id",
        "random_id",
        "project_verdict",
        "overall_verdict",
        "global_verdict",
        "replay_status",
    }
    for model in models:
        assert model.model_config["extra"] == "forbid"
        schema_text = json.dumps(model.model_json_schema(), sort_keys=True)
        assert all(f'"{name}"' not in schema_text for name in forbidden)
        payload = {
            PackageConsumerContractV1: _consumer_payload,
            MotorArmScenarioV1: _scenario_payload,
            AnalysisRequestV1: _request_payload,
            AnalysisResultV1: _result_payload,
            RunManifestV1: _manifest_payload,
        }[model]()
        payload["unexpected"] = True
        with pytest.raises(ValidationError):
            model.model_validate(payload)


def test_transport_models_do_not_contain_physics_or_finding_decisions():
    source = (APP_ROOT / "execution_contracts_v1.py").read_text(encoding="utf-8")
    forbidden = ("9.80665", "std::exp", "motor_torque(", "hold_margin >=", "random.")
    assert [token for token in forbidden if token in source] == []


def test_package_consumer_requires_the_exact_ordered_contract():
    validated = PackageConsumerContractV1.model_validate(_consumer_payload())
    assert validated.model_dump(mode="json", by_alias=True) == _consumer_payload()

    for field in (
        "required_slots",
        "validation_slots",
        "available_but_unused_slots",
        "obligation_ids",
        "applicability_ids",
    ):
        reordered = _consumer_payload()
        reordered[field] = list(reversed(reordered[field]))
        with pytest.raises(ValidationError):
            PackageConsumerContractV1.model_validate(reordered)

    duplicate = _consumer_payload()
    duplicate["validation_slots"][1] = duplicate["required_slots"][0]
    with pytest.raises(ValidationError):
        PackageConsumerContractV1.model_validate(duplicate)


@pytest.mark.parametrize(
    ("field", "bad_value"),
    [
        ("payload_mass", {"value": 0, "unit": "kg"}),
        ("arm_radius", {"value": -1, "unit": "m"}),
        ("rotation", {"value": math.inf, "unit": "rad"}),
        ("move_duration", {"value": "1.2", "unit": "s"}),
        ("hold_duration", {"value": -0.1, "unit": "s"}),
        ("ambient_temperature", {"value": math.nan, "unit": "degC"}),
        ("payload_mass", {"value": 8, "unit": "m"}),
    ],
)
def test_scenario_uses_uncoerced_finite_numbers_and_field_specific_units(
    field, bad_value
):
    payload = _scenario_payload()
    payload[field] = bad_value
    with pytest.raises(ValidationError):
        MotorArmScenarioV1.model_validate(payload)


def test_scenario_requires_confirmation_intent_and_complete_cycle():
    validated = MotorArmScenarioV1.model_validate(_scenario_payload())
    assert validated.model_dump(mode="json", by_alias=True) == _scenario_payload()

    unconfirmed = _scenario_payload()
    unconfirmed["review"]["confirmed_by_user"] = False
    with pytest.raises(ValidationError):
        MotorArmScenarioV1.model_validate(unconfirmed)

    empty_intent = _scenario_payload()
    empty_intent["review"]["intent"] = "   "
    with pytest.raises(ValidationError):
        MotorArmScenarioV1.model_validate(empty_intent)

    short_cycle = _scenario_payload()
    short_cycle["cycle_duration"]["value"] = 5
    with pytest.raises(ValidationError):
        MotorArmScenarioV1.model_validate(short_cycle)


def test_request_contains_only_hashes_bindings_and_fixed_obligations():
    validated = AnalysisRequestV1.model_validate(_request_payload())
    assert validated.model_dump(mode="json", by_alias=True) == _request_payload()

    for field, value in (
        ("package_hash", "SHA256:" + "a" * 64),
        ("scenario_hash", "sha256:" + "A" * 64),
        ("bound_cad_entity_id", ""),
        ("backend_id", "another_backend"),
    ):
        invalid = _request_payload()
        invalid[field] = value
        with pytest.raises(ValidationError):
            AnalysisRequestV1.model_validate(invalid)

    reordered = _request_payload()
    reordered["obligation_ids"] = list(reversed(OBLIGATION_IDS))
    with pytest.raises(ValidationError):
        AnalysisRequestV1.model_validate(reordered)


def test_result_is_completed_only_and_graph_consistent():
    validated = AnalysisResultV1.model_validate(_result_payload())
    assert validated.model_dump(mode="json", by_alias=True) == _result_payload()

    incomplete = _result_payload()
    incomplete["execution_disposition"] = "failed"
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(incomplete)

    bad_finding_id = _result_payload()
    bad_finding_id["obligation_outcomes"][0]["finding_id"] = "finding-1"
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(bad_finding_id)

    wrong_order = _result_payload()
    wrong_order["obligation_outcomes"] = list(
        reversed(wrong_order["obligation_outcomes"])
    )
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(wrong_order)

    wrong_request = _result_payload()
    wrong_request["obligation_outcomes"][0]["request_hash"] = HASHES["e"]
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(wrong_request)

    unknown_claim = _result_payload()
    unknown_claim["obligation_outcomes"][0]["consumed_claim_ids"] = [_uuid(999)]
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(unknown_claim)

    wrong_counts = _result_payload()
    wrong_counts["coverage"]["counts"]["pass"] = 4
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(wrong_counts)


def test_result_requires_contract_order_and_matching_quantity_units():
    for field in ("calculations", "calculation_inputs", "validation_inputs"):
        payload = _result_payload()
        container = payload if field == "calculations" else payload["consumed_inputs"]
        container[field] = list(reversed(container[field]))
        with pytest.raises(ValidationError):
            AnalysisResultV1.model_validate(payload)

    bad_calculation_unit = _result_payload()
    bad_calculation_unit["calculations"][0]["unit"] = "A"
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(bad_calculation_unit)

    bad_comparison_unit = _result_payload()
    bad_comparison_unit["obligation_outcomes"][0]["comparison_quantity"][
        "unit"
    ] = "A"
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(bad_comparison_unit)


def test_collections_and_text_are_bounded():
    too_many_limitations = _result_payload()
    too_many_limitations["limitations"] = ["bounded"] * 257
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(too_many_limitations)

    oversized_text = _result_payload()
    oversized_text["assumptions"] = ["x" * 4097]
    with pytest.raises(ValidationError):
        AnalysisResultV1.model_validate(oversized_text)


def test_stored_references_have_exactly_five_bounded_fields():
    reference = _manifest_payload()["result"]
    assert StoredObjectReferenceV1.model_validate(reference).model_dump(
        mode="json"
    ) == reference

    for field in (
        "object_hash",
        "byte_length",
        "media_type",
        "schema_id",
        "schema_version",
    ):
        missing = dict(reference)
        missing.pop(field)
        with pytest.raises(ValidationError):
            StoredObjectReferenceV1.model_validate(missing)

    oversized = dict(reference)
    oversized["byte_length"] = 8 * 1024 * 1024 + 1
    with pytest.raises(ValidationError):
        StoredObjectReferenceV1.model_validate(oversized)


def test_manifest_binds_exact_object_types_and_has_no_replay_state():
    validated = RunManifestV1.model_validate(_manifest_payload())
    assert validated.model_dump(mode="json", by_alias=True) == _manifest_payload()

    wrong_type = _manifest_payload()
    wrong_type["result"]["media_type"] = REQUEST_MEDIA_TYPE
    with pytest.raises(ValidationError):
        RunManifestV1.model_validate(wrong_type)

    replay_state = _manifest_payload()
    replay_state["replay_status"] = "exact_match"
    with pytest.raises(ValidationError):
        RunManifestV1.model_validate(replay_state)


def test_checked_in_program_01b_schemas_are_current_and_validate_examples():
    from scripts.export_contract_schemas import render_schemas

    rendered = render_schemas()
    examples = {
        "package-consumer-contract-v1.schema.json": _consumer_payload(),
        "motor-arm-scenario-v1.schema.json": _scenario_payload(),
        "analysis-request-v1.schema.json": _request_payload(),
        "analysis-result-v1.schema.json": _result_payload(),
        "run-manifest-v1.schema.json": _manifest_payload(),
    }
    for filename, example in examples.items():
        payload = rendered[filename]
        assert (ROOT / "schemas" / filename).read_bytes() == payload
        schema = json.loads(payload)
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema).validate(example)
        assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        assert schema["$id"] == example["$schema"]

        invalid = deepcopy(example)
        invalid["unknown"] = True
        assert list(Draft202012Validator(schema).iter_errors(invalid))
