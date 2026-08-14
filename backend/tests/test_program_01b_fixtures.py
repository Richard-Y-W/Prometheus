from __future__ import annotations

import hashlib
import json
from pathlib import Path

from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import ExecutionComponentV2
from app.execution_contracts_v1 import (
    APPLICABILITY_IDS,
    BACKEND_CONTRACT_VERSION,
    BACKEND_ID,
    CONSUMER_MEDIA_TYPE,
    OBLIGATION_IDS,
    REQUIRED_SLOT_SPECS,
    VALIDATION_SLOT_SPECS,
    AVAILABLE_BUT_UNUSED_SLOT_SPECS,
    PackageConsumerContractV1,
)
from app.fixture_catalog_v2 import FIXTURE_IDS, get_fixture_definition
from scripts.export_contract_fixture import render_fixture_files
from scripts.export_program_01b_fixtures import render_program_01b_files


ROOT = Path(__file__).parents[2]
CONTRACT_ROOT = ROOT / "fixtures/contracts"
MOTOR_IDS = (
    "prometheus.motor-a.fixture-1",
    "prometheus.motor-b.fixture-1",
)


def _hash(source: bytes) -> str:
    return f"sha256:{hashlib.sha256(source).hexdigest()}"


def _read_package(stem: str) -> tuple[dict[str, object], bytes, str]:
    human = json.loads((CONTRACT_ROOT / f"{stem}.json").read_bytes())
    canonical = (CONTRACT_ROOT / f"{stem}.jcs").read_bytes()
    identity = (CONTRACT_ROOT / f"{stem}.sha256").read_text(
        encoding="ascii"
    ).strip()
    return human, canonical, identity


def _normalized_parameter_vector(package: dict[str, object]) -> dict[str, object]:
    """Exclude only IDs, provenance, and hashes from the semantic comparison."""

    claims = {claim["claim_id"]: claim for claim in package["claims"]}
    return {
        slot["name"]: {
            "quantity": slot["quantity"],
            "dimension": slot["dimension"],
            "required_for_execution": slot["required_for_execution"],
            "value_state": claims[slot["selected_claim_id"]]["value_state"],
            "value": claims[slot["selected_claim_id"]]["value"],
            "unit": claims[slot["selected_claim_id"]].get("unit"),
        }
        for slot in package["parameter_slots"]
    }


def test_catalog_is_closed_and_every_artifact_hash_covers_exact_bytes():
    assert FIXTURE_IDS == (
        "prometheus.motor-a.fixture-1",
        "prometheus.motor-b.fixture-1",
        "prometheus.pm-36-gm.fixture-2",
    )
    for fixture_id in FIXTURE_IDS:
        definition = get_fixture_definition(fixture_id)
        assert definition.fixture_id == fixture_id
        source_bytes = definition.source_path.read_bytes()
        assert definition.source_hash == _hash(source_bytes)
        source = json.loads(source_bytes)
        assert source["fixture_id"] == fixture_id
        assert source["manufacturer"] == definition.manufacturer
        assert source["part_number"] == definition.part_number
        assert source["revision"] == definition.revision
        assert source["component_class"] == definition.component_class

        if fixture_id in MOTOR_IDS:
            consumer_bytes = definition.consumer_path.read_bytes()
            assert definition.consumer_hash == _hash(consumer_bytes)
            assert definition.consumer_media_type == CONSUMER_MEDIA_TYPE
        else:
            assert definition.consumer_path is None
            assert definition.consumer_hash is None
            assert definition.consumer_media_type is None


def test_consumer_artifact_is_exact_closed_and_synthetic_only():
    semantic, canonical, identity = _read_package(
        "package-consumer.motor-arm-builtin-v1"
    )
    validated = PackageConsumerContractV1.model_validate(semantic).model_dump(
        mode="json", by_alias=True
    )
    assert canonicalize_value(validated) == canonical
    assert object_hash(canonical) == identity
    assert canonical == (CONTRACT_ROOT / "package-consumer.motor-arm-builtin-v1.jcs").read_bytes()
    assert semantic["backend"] == {
        "backend_id": BACKEND_ID,
        "contract_version": BACKEND_CONTRACT_VERSION,
    }
    assert [
        (
            slot["slot_name"],
            slot["engineering_quantity"],
            slot["dimension"],
            slot["value_shape"],
            slot["canonical_unit"],
        )
        for slot in semantic["required_slots"]
    ] == list(REQUIRED_SLOT_SPECS)
    assert [
        (
            slot["slot_name"],
            slot["engineering_quantity"],
            slot["dimension"],
            slot["value_shape"],
            slot["canonical_unit"],
        )
        for slot in semantic["validation_slots"]
    ] == list(VALIDATION_SLOT_SPECS)
    assert [
        (
            slot["slot_name"],
            slot["engineering_quantity"],
            slot["dimension"],
            slot["value_shape"],
            slot["canonical_unit"],
        )
        for slot in semantic["available_but_unused_slots"]
    ] == list(AVAILABLE_BUT_UNUSED_SLOT_SPECS)
    assert semantic["obligation_ids"] == list(OBLIGATION_IDS)
    assert semantic["applicability_ids"] == list(APPLICABILITY_IDS)
    assert semantic["validation_level"] == "synthetic_conformance_only"


def test_motor_evidence_differs_only_in_identity_and_continuous_torque():
    sources = [
        json.loads(get_fixture_definition(fixture_id).source_path.read_bytes())
        for fixture_id in MOTOR_IDS
    ]
    for source, fixture_id, part_number in zip(
        sources, MOTOR_IDS, ("DC-GM-A", "DC-GM-B"), strict=True
    ):
        assert source["fixture_id"] == fixture_id
        assert source["manufacturer"] == "Prometheus Fixture Works"
        assert source["part_number"] == part_number
        assert source["revision"] == "fixture-1"
        assert source["component_class"] == "dc_gearmotor"
        assert source["source_authority"] == "synthetic_fixture"
        assert source["physical_validation_status"] == "unvalidated"
        assert len(source["parameters"]) == 17
        assert all(
            parameter["validity_conditions"]
            == ["synthetic conformance fixture only"]
            for parameter in source["parameters"]
        )
        assert any("no physical validation" in text.lower() for text in source["limitations"])

    normalized = []
    for source in sources:
        normalized.append(
            {
                parameter["name"]: {
                    key: value
                    for key, value in parameter.items()
                    if key not in {"source_locator", "original_value"}
                }
                for parameter in source["parameters"]
            }
        )
    a_torque = normalized[0].pop("continuous_torque_nm")
    b_torque = normalized[1].pop("continuous_torque_nm")
    assert normalized[0] == normalized[1]
    assert a_torque["value"] == {"kind": "scalar", "value": 0.208}
    assert b_torque["value"] == {"kind": "scalar", "value": 0.32}
    a_torque["value"] = b_torque["value"]
    assert a_torque == b_torque


def test_new_packages_are_exact_ready_consumer_inputs_and_old_package_is_unchanged():
    old_human, old_canonical, old_identity = _read_package(
        "execution-component-v2.pm-36-gm"
    )
    assert render_fixture_files() == {
        "execution-component-v2.pm-36-gm.json": (
            CONTRACT_ROOT / "execution-component-v2.pm-36-gm.json"
        ).read_bytes(),
        "execution-component-v2.pm-36-gm.jcs": old_canonical,
        "execution-component-v2.pm-36-gm.sha256": (old_identity + "\n").encode(
            "ascii"
        ),
    }
    assert old_human["capability_id"] == "component_input.pm_36_gm"
    assert old_human["execution_readiness"] == "blocked"
    old_consumer_gate = next(
        gate
        for gate in old_human["gates"]
        if gate["required_review_type"] == "package_consumer"
    )
    assert old_consumer_gate["state"] == "blocked"
    assert old_consumer_gate["satisfying_reference_ids"] == []

    consumer_identity = (
        CONTRACT_ROOT / "package-consumer.motor-arm-builtin-v1.sha256"
    ).read_text(encoding="ascii").strip()
    packages = []
    for motor in ("motor-a", "motor-b"):
        human, canonical, identity = _read_package(
            f"execution-component-v2.{motor}"
        )
        assert ExecutionComponentV2.model_validate(human).model_dump(
            mode="json", by_alias=True
        ) == human
        assert canonicalize_value(human) == canonical
        assert object_hash(canonical) == identity
        assert human["package_compiler"] == {
            "name": "prometheus_python",
            "version": "0.2.0",
        }
        assert human["capability_id"] == "component_input.dc_gearmotor_v1"
        assert human["execution_readiness"] == "ready"
        assert sorted(
            (artifact["artifact_role"], artifact["media_type"], artifact["artifact_hash"])
            for artifact in human["artifacts"]
        ) == sorted(
            [
                (
                    "source_evidence",
                    "application/json",
                    get_fixture_definition(
                        f"prometheus.{motor}.fixture-1"
                    ).source_hash,
                ),
                ("supporting_input", CONSUMER_MEDIA_TYPE, consumer_identity),
            ]
        )
        consumer_gate = next(
            gate
            for gate in human["gates"]
            if gate["required_review_type"] == "package_consumer"
        )
        assert consumer_gate["phase"] == "execution"
        assert consumer_gate["state"] == "satisfied"
        assert consumer_gate["satisfying_reference_ids"] == [consumer_identity]
        assert any(
            "synthetic" in item["statement"].lower()
            and "physical" in item["statement"].lower()
            for item in human["limitations"]
        )
        packages.append(human)

    normalized = [_normalized_parameter_vector(package) for package in packages]
    a_torque = normalized[0].pop("continuous_torque_nm")
    b_torque = normalized[1].pop("continuous_torque_nm")
    assert normalized[0] == normalized[1]
    assert a_torque["value"] == {"kind": "scalar", "value": 0.208}
    assert b_torque["value"] == {"kind": "scalar", "value": 0.32}
    a_torque["value"] = b_torque["value"]
    assert a_torque == b_torque


def test_program_01b_exporter_reproduces_all_nine_exact_files():
    rendered = render_program_01b_files()
    assert set(rendered) == {
        "package-consumer.motor-arm-builtin-v1.json",
        "package-consumer.motor-arm-builtin-v1.jcs",
        "package-consumer.motor-arm-builtin-v1.sha256",
        "execution-component-v2.motor-a.json",
        "execution-component-v2.motor-a.jcs",
        "execution-component-v2.motor-a.sha256",
        "execution-component-v2.motor-b.json",
        "execution-component-v2.motor-b.jcs",
        "execution-component-v2.motor-b.sha256",
    }
    for filename, payload in rendered.items():
        assert (CONTRACT_ROOT / filename).read_bytes() == payload
