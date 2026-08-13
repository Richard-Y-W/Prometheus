"""Closed catalog for exact local Program 01A/01B fixture ingestion."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Literal, TypeAlias

from .execution_contracts_v1 import (
    CONSUMER_MEDIA_TYPE,
    REQUIRED_SLOT_SPECS,
    VALIDATION_SLOT_SPECS,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = REPOSITORY_ROOT / "fixtures/evidence"
CONTRACT_ROOT = REPOSITORY_ROOT / "fixtures/contracts"

MOTOR_A_FIXTURE_ID = "prometheus.motor-a.fixture-1"
MOTOR_B_FIXTURE_ID = "prometheus.motor-b.fixture-1"
PM_36_FIXTURE_ID = "prometheus.pm-36-gm.fixture-2"
FIXTURE_IDS = (MOTOR_A_FIXTURE_ID, MOTOR_B_FIXTURE_ID, PM_36_FIXTURE_ID)
FixtureIdV2: TypeAlias = Literal[
    "prometheus.motor-a.fixture-1",
    "prometheus.motor-b.fixture-1",
    "prometheus.pm-36-gm.fixture-2",
]

DC_GEARMOTOR_CAPABILITY = "component_input.dc_gearmotor_v1"
PM_36_CAPABILITY = "component_input.pm_36_gm"
MOTOR_REQUIRED_FOR_EXECUTION = frozenset(
    spec[0] for spec in (*REQUIRED_SLOT_SPECS, *VALIDATION_SLOT_SPECS)
)
PM_36_REQUIRED_FOR_EXECUTION = frozenset(
    {
        *MOTOR_REQUIRED_FOR_EXECUTION,
        "nominal_voltage_v",
        "supply_current_limit_a",
    }
)
CONSUMER_PATH = CONTRACT_ROOT / "package-consumer.motor-arm-builtin-v1.jcs"
CONSUMER_HASH = (
    "sha256:e185ee08c987b30cf20d69af06a8754224f25068e499b052a49566c137bd0155"
)


@dataclass(frozen=True, slots=True)
class FixtureDefinition:
    fixture_id: FixtureIdV2
    source_path: Path
    source_hash: str
    manufacturer: str
    part_number: str
    revision: str
    component_class: str
    family: str
    capability_id: str
    required_for_execution: frozenset[str]
    execution_limitation: str
    consumer_path: Path | None
    consumer_hash: str | None
    consumer_media_type: str | None
    consumer_gate_state: Literal["satisfied", "blocked"]
    consumer_gate_reason: str | None


_DEFINITIONS = MappingProxyType(
    {
        MOTOR_A_FIXTURE_ID: FixtureDefinition(
            fixture_id=MOTOR_A_FIXTURE_ID,
            source_path=EVIDENCE_ROOT / "motor-a.synthetic-v1.json",
            source_hash=(
                "sha256:00189c149eba94e1640a9a7a6d24c148"
                "9606b9472ff38ba2fc9a134ed75bbed3"
            ),
            manufacturer="Prometheus Fixture Works",
            part_number="DC-GM-A",
            revision="fixture-1",
            component_class="dc_gearmotor",
            family="synthetic dc gearmotor",
            capability_id=DC_GEARMOTOR_CAPABILITY,
            required_for_execution=MOTOR_REQUIRED_FOR_EXECUTION,
            execution_limitation=(
                "The reviewed consumer accepts this synthetic conformance package "
                "only; no physical validity is implied."
            ),
            consumer_path=CONSUMER_PATH,
            consumer_hash=CONSUMER_HASH,
            consumer_media_type=CONSUMER_MEDIA_TYPE,
            consumer_gate_state="satisfied",
            consumer_gate_reason=None,
        ),
        MOTOR_B_FIXTURE_ID: FixtureDefinition(
            fixture_id=MOTOR_B_FIXTURE_ID,
            source_path=EVIDENCE_ROOT / "motor-b.synthetic-v1.json",
            source_hash=(
                "sha256:24f500104783b8d87f6fa42608c20ec6"
                "851f2f7ca0872400a14b6971b88f97df"
            ),
            manufacturer="Prometheus Fixture Works",
            part_number="DC-GM-B",
            revision="fixture-1",
            component_class="dc_gearmotor",
            family="synthetic dc gearmotor",
            capability_id=DC_GEARMOTOR_CAPABILITY,
            required_for_execution=MOTOR_REQUIRED_FOR_EXECUTION,
            execution_limitation=(
                "The reviewed consumer accepts this synthetic conformance package "
                "only; no physical validity is implied."
            ),
            consumer_path=CONSUMER_PATH,
            consumer_hash=CONSUMER_HASH,
            consumer_media_type=CONSUMER_MEDIA_TYPE,
            consumer_gate_state="satisfied",
            consumer_gate_reason=None,
        ),
        PM_36_FIXTURE_ID: FixtureDefinition(
            fixture_id=PM_36_FIXTURE_ID,
            source_path=EVIDENCE_ROOT / "pm-36-gm.synthetic-v2.json",
            source_hash=(
                "sha256:2efe91276f0e9950fee8ae651c96b592"
                "48734e79f01e83fd4b5f638318d0a684"
            ),
            manufacturer="Prometheus Fixture Works",
            part_number="PM-36-GM",
            revision="fixture-2",
            component_class="gearmotor",
            family="synthetic gearmotor",
            capability_id=PM_36_CAPABILITY,
            required_for_execution=PM_36_REQUIRED_FOR_EXECUTION,
            execution_limitation=(
                "Program 01A validates reviewed input identity; it does not "
                "execute an engineering solver."
            ),
            consumer_path=None,
            consumer_hash=None,
            consumer_media_type=None,
            consumer_gate_state="blocked",
            consumer_gate_reason=(
                "Program 01A has no v2 package consumer or solver execution."
            ),
        ),
    }
)


def get_fixture_definition(fixture_id: str) -> FixtureDefinition:
    """Resolve one exact ASCII catalog ID without accepting caller policy."""

    return _DEFINITIONS[fixture_id]


__all__ = [
    "CONSUMER_HASH",
    "DC_GEARMOTOR_CAPABILITY",
    "FIXTURE_IDS",
    "FixtureDefinition",
    "FixtureIdV2",
    "MOTOR_A_FIXTURE_ID",
    "MOTOR_B_FIXTURE_ID",
    "PM_36_FIXTURE_ID",
    "get_fixture_definition",
]
