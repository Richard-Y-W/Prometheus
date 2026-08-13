"""Closed Program 01B transport contracts.

These models validate supplied transport values and immutable object graphs.
They deliberately contain no engineering equations or outcome construction.
"""

from __future__ import annotations

from typing import Annotated, Literal, TypeAlias

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    StrictBool,
    StrictInt,
    StrictStr,
    StringConstraints,
    model_validator,
)

from app.contracts_v2 import AsciiName, HashId, JsonNumber, NonEmptyText, UuidV4


CONSUMER_SCHEMA_ID = (
    "urn:prometheus:schema:package-consumer-contract:1.0.0"
)
SCENARIO_SCHEMA_ID = "urn:prometheus:schema:motor-arm-scenario:1.0.0"
REQUEST_SCHEMA_ID = "urn:prometheus:schema:analysis-request:1.0.0"
RESULT_SCHEMA_ID = "urn:prometheus:schema:analysis-result:1.0.0"
MANIFEST_SCHEMA_ID = "urn:prometheus:schema:run-manifest:1.0.0"

CONSUMER_MEDIA_TYPE = (
    "application/vnd.prometheus.package-consumer-contract+json;version=1.0.0"
)
SCENARIO_MEDIA_TYPE = (
    "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0"
)
REQUEST_MEDIA_TYPE = (
    "application/vnd.prometheus.analysis-request+json;version=1.0.0"
)
RESULT_MEDIA_TYPE = (
    "application/vnd.prometheus.analysis-result+json;version=1.0.0"
)
MANIFEST_MEDIA_TYPE = (
    "application/vnd.prometheus.run-manifest+json;version=1.0.0"
)
PACKAGE_MEDIA_TYPE = (
    "application/vnd.prometheus.execution-component+json;version=2.0.0"
)
PACKAGE_SCHEMA_ID = "urn:prometheus:schema:execution-component:2.0.0"

BACKEND_ID = "motor_arm_builtin_v1"
BACKEND_CONTRACT_VERSION = "1.0.0"

OBLIGATION_IDS = (
    "motor_arm.move_torque_speed",
    "motor_arm.hold_continuous_torque",
    "motor_arm.driver_current_limit",
    "motor_arm.thermal_peak",
)
APPLICABILITY_IDS = (
    "point_payload_at_reviewed_radius",
    "horizontal_gravity_loading",
    "symmetric_triangular_velocity",
    "linear_torque_speed_model",
    "algebraic_current_estimate",
    "one_node_periodic_rc_thermal_model",
)
CALCULATION_IDS = (
    "holding_load_torque",
    "acceleration_load_torque",
    "required_hold_motor_torque",
    "required_move_motor_torque",
    "peak_motor_speed",
    "available_move_torque",
    "estimated_move_current",
    "estimated_peak_temperature",
)

REQUIRED_SLOT_SPECS = (
    ("gear_ratio", "ratio", "dimensionless", "scalar", "1"),
    (
        "gearbox_efficiency_nominal",
        "efficiency",
        "dimensionless",
        "scalar",
        "1",
    ),
    ("continuous_torque_nm", "torque", "torque", "scalar", "N*m"),
    ("stall_torque_nm", "torque", "torque", "scalar", "N*m"),
    (
        "no_load_speed_rad_s",
        "angular_velocity",
        "angle/time",
        "scalar",
        "rad/s",
    ),
    (
        "no_load_current_a",
        "electric_current",
        "electric_current",
        "scalar",
        "A",
    ),
    (
        "torque_constant_nm_a",
        "torque_constant",
        "torque/electric_current",
        "scalar",
        "N*m/A",
    ),
    (
        "driver_current_limit_a",
        "electric_current_limit",
        "electric_current",
        "scalar",
        "A",
    ),
    (
        "winding_resistance_ohm",
        "electrical_resistance",
        "electric_resistance",
        "scalar",
        "ohm",
    ),
    (
        "thermal_resistance_k_w",
        "thermal_resistance",
        "temperature/power",
        "scalar",
        "K/W",
    ),
    (
        "thermal_capacitance_j_k",
        "heat_capacity",
        "energy/temperature",
        "scalar",
        "J/K",
    ),
    (
        "maximum_temperature_c",
        "temperature_limit",
        "temperature",
        "scalar",
        "degC",
    ),
)
VALIDATION_SLOT_SPECS = (
    (
        "gearbox_efficiency_range",
        "efficiency",
        "dimensionless",
        "range",
        "1",
    ),
    (
        "torque_speed_curve",
        "torque_by_angular_velocity",
        "torque",
        "curve",
        "N*m",
    ),
)
AVAILABLE_BUT_UNUSED_SLOT_SPECS = (
    (
        "nominal_voltage_v",
        "voltage",
        "electric_potential",
        "scalar",
        "V",
    ),
    (
        "supply_current_limit_a",
        "electric_current_limit",
        "electric_current",
        "scalar",
        "A",
    ),
    (
        "gearbox_lifetime",
        "service_life",
        "time",
        "scalar_or_unknown",
        "h",
    ),
)

CALCULATION_UNITS = (
    "N*m",
    "N*m",
    "N*m",
    "N*m",
    "rad/s",
    "N*m",
    "A",
    "degC",
)
OBLIGATION_UNITS = ("N*m", "N*m", "A", "degC")
MAX_OBJECT_BYTES = 8 * 1024 * 1024
MAX_RESULT_COLLECTION = 256


class ExecutionContractV1(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)


BoundedText = Annotated[
    StrictStr,
    StringConstraints(strip_whitespace=True, min_length=1, max_length=4096),
]
ShortIdentity = Annotated[
    StrictStr,
    StringConstraints(strip_whitespace=True, min_length=1, max_length=512),
]
BoundedTextList: TypeAlias = Annotated[
    list[BoundedText], Field(max_length=MAX_RESULT_COLLECTION)
]


def _slot_identity(slot: "ConsumerSlotV1") -> tuple[str, str, str, str, str]:
    return (
        slot.slot_name,
        slot.engineering_quantity,
        slot.dimension,
        slot.value_shape,
        slot.canonical_unit,
    )


class BackendContractV1(ExecutionContractV1):
    backend_id: Literal[BACKEND_ID]
    contract_version: Literal[BACKEND_CONTRACT_VERSION]


class AcceptedPackageV1(ExecutionContractV1):
    schema_id: Literal[PACKAGE_SCHEMA_ID]
    schema_version: Literal["2.0.0"]
    package_kind: Literal["component_execution_input"]
    capability: Literal["component_input.dc_gearmotor_v1"]


class ConsumerSlotV1(ExecutionContractV1):
    slot_name: AsciiName
    engineering_quantity: AsciiName
    dimension: ShortIdentity
    value_shape: Literal["scalar", "range", "curve", "scalar_or_unknown"]
    canonical_unit: ShortIdentity


class SupportedScenarioV1(ExecutionContractV1):
    schema_id: Literal[SCENARIO_SCHEMA_ID]
    schema_version: Literal["1.0.0"]
    scenario_kind: Literal["motor_arm"]
    motion_profiles: Annotated[
        list[Literal["symmetric_triangular_velocity"]],
        Field(min_length=1, max_length=1),
    ]


class PackageConsumerContractV1(ExecutionContractV1):
    schema_id: Literal[CONSUMER_SCHEMA_ID] = Field(alias="$schema")
    schema_version: Literal["1.0.0"]
    contract_kind: Literal["package_consumer"]
    backend: BackendContractV1
    accepted_package: AcceptedPackageV1
    required_slots: Annotated[
        list[ConsumerSlotV1],
        Field(min_length=len(REQUIRED_SLOT_SPECS), max_length=len(REQUIRED_SLOT_SPECS)),
    ]
    validation_slots: Annotated[
        list[ConsumerSlotV1],
        Field(
            min_length=len(VALIDATION_SLOT_SPECS),
            max_length=len(VALIDATION_SLOT_SPECS),
        ),
    ]
    available_but_unused_slots: Annotated[
        list[ConsumerSlotV1],
        Field(
            min_length=len(AVAILABLE_BUT_UNUSED_SLOT_SPECS),
            max_length=len(AVAILABLE_BUT_UNUSED_SLOT_SPECS),
        ),
    ]
    supported_scenario: SupportedScenarioV1
    obligation_ids: Annotated[list[StrictStr], Field(min_length=4, max_length=4)]
    applicability_ids: Annotated[list[StrictStr], Field(min_length=6, max_length=6)]
    validation_level: Literal["synthetic_conformance_only"]

    @model_validator(mode="after")
    def require_exact_contract(self):
        actual_groups = (
            tuple(_slot_identity(slot) for slot in self.required_slots),
            tuple(_slot_identity(slot) for slot in self.validation_slots),
            tuple(_slot_identity(slot) for slot in self.available_but_unused_slots),
        )
        expected_groups = (
            REQUIRED_SLOT_SPECS,
            VALIDATION_SLOT_SPECS,
            AVAILABLE_BUT_UNUSED_SLOT_SPECS,
        )
        if actual_groups != expected_groups:
            raise ValueError("consumer slots must match the contract order and metadata")
        slot_names = [slot[0] for group in actual_groups for slot in group]
        if len(slot_names) != len(set(slot_names)):
            raise ValueError("consumer slot names must be unique across classifications")
        if tuple(self.obligation_ids) != OBLIGATION_IDS:
            raise ValueError("obligation_ids must use the fixed contract order")
        if tuple(self.applicability_ids) != APPLICABILITY_IDS:
            raise ValueError("applicability_ids must use the fixed contract order")
        return self


PositiveNumber = Annotated[JsonNumber, Field(gt=0)]
NonNegativeNumber = Annotated[JsonNumber, Field(ge=0)]


class KilogramsQuantityV1(ExecutionContractV1):
    value: PositiveNumber
    unit: Literal["kg"]


class MetresQuantityV1(ExecutionContractV1):
    value: PositiveNumber
    unit: Literal["m"]


class RadiansQuantityV1(ExecutionContractV1):
    value: PositiveNumber
    unit: Literal["rad"]


class PositiveSecondsQuantityV1(ExecutionContractV1):
    value: PositiveNumber
    unit: Literal["s"]


class NonNegativeSecondsQuantityV1(ExecutionContractV1):
    value: NonNegativeNumber
    unit: Literal["s"]


class TemperatureQuantityV1(ExecutionContractV1):
    value: JsonNumber
    unit: Literal["degC"]


class ScenarioReviewV1(ExecutionContractV1):
    confirmed_by_user: StrictBool
    intent: NonEmptyText

    @model_validator(mode="after")
    def require_confirmation(self):
        if self.confirmed_by_user is not True:
            raise ValueError("scenario must be explicitly confirmed by the user")
        return self


class MotorArmScenarioV1(ExecutionContractV1):
    schema_id: Literal[SCENARIO_SCHEMA_ID] = Field(alias="$schema")
    schema_version: Literal["1.0.0"]
    scenario_kind: Literal["motor_arm"]
    payload_mass: KilogramsQuantityV1
    arm_radius: MetresQuantityV1
    rotation: RadiansQuantityV1
    move_duration: PositiveSecondsQuantityV1
    hold_duration: NonNegativeSecondsQuantityV1
    cycle_duration: PositiveSecondsQuantityV1
    ambient_temperature: TemperatureQuantityV1
    motion_profile: Literal["symmetric_triangular_velocity"]
    review: ScenarioReviewV1

    @model_validator(mode="after")
    def require_complete_cycle(self):
        if self.cycle_duration.value < (
            self.move_duration.value + self.hold_duration.value
        ):
            raise ValueError("cycle_duration must cover move_duration plus hold_duration")
        return self


class AnalysisRequestV1(ExecutionContractV1):
    schema_id: Literal[REQUEST_SCHEMA_ID] = Field(alias="$schema")
    schema_version: Literal["1.0.0"]
    request_kind: Literal["motor_arm_analysis"]
    package_hash: HashId
    scenario_hash: HashId
    assembly_artifact_hash: HashId
    bound_cad_entity_id: BoundedText
    backend_id: Literal[BACKEND_ID]
    backend_contract_version: Literal[BACKEND_CONTRACT_VERSION]
    package_consumer_contract_hash: HashId
    obligation_ids: Annotated[list[StrictStr], Field(min_length=4, max_length=4)]

    @model_validator(mode="after")
    def require_obligation_order(self):
        if tuple(self.obligation_ids) != OBLIGATION_IDS:
            raise ValueError("obligation_ids must use the fixed contract order")
        return self


class PlatformIdentityV1(ExecutionContractV1):
    name: ShortIdentity
    release: ShortIdentity
    architecture: ShortIdentity


class ToolIdentityV1(ExecutionContractV1):
    id: ShortIdentity
    version: ShortIdentity


class FloatingPointPolicyV1(ExecutionContractV1):
    contraction: Literal["disabled"]
    fast_math: StrictBool
    rounding_mode: Literal["to_nearest"]

    @model_validator(mode="after")
    def reject_fast_math(self):
        if self.fast_math is not False:
            raise ValueError("fast_math must be false")
        return self


class NumericProfileV1(ExecutionContractV1):
    operating_system: PlatformIdentityV1
    compiler: ToolIdentityV1
    standard_library: ToolIdentityV1
    math_runtime: ToolIdentityV1
    backend_build_fingerprint: HashId
    floating_point: FloatingPointPolicyV1
    numeric_serialization_version: Literal["1.0.0"]


class ExecutionBackendV1(ExecutionContractV1):
    backend_id: Literal[BACKEND_ID]
    contract_version: Literal[BACKEND_CONTRACT_VERSION]
    numeric_profile: NumericProfileV1


class CalculationV1(ExecutionContractV1):
    calculation_id: AsciiName
    value: JsonNumber
    unit: ShortIdentity


InputUse = Literal[
    "calculation_input", "validation_input", "available_but_unused"
]


class ConsumedInputV1(ExecutionContractV1):
    slot_name: AsciiName
    claim_id: UuidV4
    input_use: InputUse


class ConsumedInputsV1(ExecutionContractV1):
    calculation_inputs: Annotated[
        list[ConsumedInputV1],
        Field(min_length=len(REQUIRED_SLOT_SPECS), max_length=len(REQUIRED_SLOT_SPECS)),
    ]
    validation_inputs: Annotated[
        list[ConsumedInputV1],
        Field(
            min_length=len(VALIDATION_SLOT_SPECS),
            max_length=len(VALIDATION_SLOT_SPECS),
        ),
    ]
    available_but_unused: Annotated[
        list[ConsumedInputV1],
        Field(
            min_length=len(AVAILABLE_BUT_UNUSED_SLOT_SPECS),
            max_length=len(AVAILABLE_BUT_UNUSED_SLOT_SPECS),
        ),
    ]

    @model_validator(mode="after")
    def require_contract_order_and_classification(self):
        groups = (
            (self.calculation_inputs, REQUIRED_SLOT_SPECS, "calculation_input"),
            (self.validation_inputs, VALIDATION_SLOT_SPECS, "validation_input"),
            (
                self.available_but_unused,
                AVAILABLE_BUT_UNUSED_SLOT_SPECS,
                "available_but_unused",
            ),
        )
        for inputs, specs, expected_use in groups:
            if tuple(item.slot_name for item in inputs) != tuple(
                spec[0] for spec in specs
            ):
                raise ValueError("consumed inputs must use contract slot order")
            if any(item.input_use != expected_use for item in inputs):
                raise ValueError("consumed input use must match its collection")
        claim_ids = [str(item.claim_id) for inputs, _, _ in groups for item in inputs]
        if len(claim_ids) != len(set(claim_ids)):
            raise ValueError("consumed claim IDs must be unique")
        return self


EfficiencyNumber = Annotated[JsonNumber, Field(ge=0, le=1)]


class EfficiencyRangeSensitivityV1(ExecutionContractV1):
    sensitivity_id: Literal["gearbox_efficiency_range"]
    claim_id: UuidV4
    minimum_efficiency: EfficiencyNumber
    maximum_efficiency: EfficiencyNumber
    hold_margin_at_minimum: JsonNumber
    hold_margin_at_maximum: JsonNumber
    crosses_zero: StrictBool

    @model_validator(mode="after")
    def require_ordered_efficiency(self):
        if self.minimum_efficiency > self.maximum_efficiency:
            raise ValueError("minimum_efficiency must not exceed maximum_efficiency")
        return self


class ReportedQuantityV1(ExecutionContractV1):
    value: JsonNumber
    unit: ShortIdentity


class ObligationOutcomeV1(ExecutionContractV1):
    finding_id: HashId
    obligation_id: StrictStr
    outcome: Literal["pass", "fail", "indeterminate", "not_evaluated"]
    severity: Literal["info", "caution", "error"]
    title: BoundedText
    mechanism: BoundedText
    calculated_quantity: ReportedQuantityV1
    comparison_quantity: ReportedQuantityV1
    comparison_operator: Literal[">="]
    signed_margin: JsonNumber
    package_hash: HashId
    request_hash: HashId
    scenario_hash: HashId
    consumed_claim_ids: Annotated[
        list[UuidV4], Field(min_length=1, max_length=MAX_RESULT_COLLECTION)
    ]
    assumptions: BoundedTextList
    limitations: BoundedTextList

    @model_validator(mode="after")
    def require_unique_claims_and_matching_units(self):
        claim_ids = [str(claim_id) for claim_id in self.consumed_claim_ids]
        if len(claim_ids) != len(set(claim_ids)):
            raise ValueError("finding consumed_claim_ids must be unique")
        if self.calculated_quantity.unit != self.comparison_quantity.unit:
            raise ValueError("finding comparison quantities must use the same unit")
        return self


class MissingInformationV1(ExecutionContractV1):
    question_id: StrictStr
    reason: BoundedText


NonNegativeCount = Annotated[StrictInt, Field(ge=0, le=len(OBLIGATION_IDS))]


class OutcomeCountsV1(ExecutionContractV1):
    pass_count: NonNegativeCount = Field(alias="pass")
    fail_count: NonNegativeCount = Field(alias="fail")
    indeterminate: NonNegativeCount
    not_evaluated: NonNegativeCount


class CoverageV1(ExecutionContractV1):
    requested_obligations: Literal[4]
    evaluated_obligations: Literal[4]
    counts: OutcomeCountsV1
    known_uncovered_questions: Annotated[
        list[MissingInformationV1], Field(min_length=1, max_length=1)
    ]

    @model_validator(mode="after")
    def require_bounded_question_and_total(self):
        total = (
            self.counts.pass_count
            + self.counts.fail_count
            + self.counts.indeterminate
            + self.counts.not_evaluated
        )
        if total != len(OBLIGATION_IDS):
            raise ValueError("coverage counts must sum to requested obligations")
        if self.known_uncovered_questions[0].question_id != (
            "assembly.center_of_gravity"
        ):
            raise ValueError("the fixed uncovered question must be center of gravity")
        return self


class AnalysisResultV1(ExecutionContractV1):
    schema_id: Literal[RESULT_SCHEMA_ID] = Field(alias="$schema")
    schema_version: Literal["1.0.0"]
    execution_disposition: Literal["completed"]
    request_hash: HashId
    package_hash: HashId
    backend: ExecutionBackendV1
    calculations: Annotated[
        list[CalculationV1],
        Field(min_length=len(CALCULATION_IDS), max_length=len(CALCULATION_IDS)),
    ]
    consumed_inputs: ConsumedInputsV1
    sensitivities: Annotated[
        list[EfficiencyRangeSensitivityV1], Field(min_length=1, max_length=1)
    ]
    obligation_outcomes: Annotated[
        list[ObligationOutcomeV1],
        Field(min_length=len(OBLIGATION_IDS), max_length=len(OBLIGATION_IDS)),
    ]
    missing_information: Annotated[
        list[MissingInformationV1], Field(max_length=MAX_RESULT_COLLECTION)
    ]
    assumptions: BoundedTextList
    limitations: BoundedTextList
    applicability: Annotated[
        list[StrictStr],
        Field(min_length=len(APPLICABILITY_IDS), max_length=len(APPLICABILITY_IDS)),
    ]
    coverage: CoverageV1

    @model_validator(mode="after")
    def require_result_graph_consistency(self):
        calculation_shape = tuple(
            (item.calculation_id, item.unit) for item in self.calculations
        )
        if calculation_shape != tuple(zip(CALCULATION_IDS, CALCULATION_UNITS)):
            raise ValueError("calculations must use fixed order and units")
        if tuple(self.applicability) != APPLICABILITY_IDS:
            raise ValueError("applicability must use fixed contract order")
        if tuple(item.obligation_id for item in self.obligation_outcomes) != (
            OBLIGATION_IDS
        ):
            raise ValueError("obligation outcomes must use fixed contract order")

        all_claim_ids = {
            str(item.claim_id)
            for collection in (
                self.consumed_inputs.calculation_inputs,
                self.consumed_inputs.validation_inputs,
                self.consumed_inputs.available_but_unused,
            )
            for item in collection
        }
        for index, outcome in enumerate(self.obligation_outcomes):
            if outcome.request_hash != self.request_hash:
                raise ValueError("finding request hash must match the result")
            if outcome.package_hash != self.package_hash:
                raise ValueError("finding package hash must match the result")
            if outcome.calculated_quantity.unit != OBLIGATION_UNITS[index]:
                raise ValueError("finding unit must match its obligation")
            if not {str(value) for value in outcome.consumed_claim_ids}.issubset(
                all_claim_ids
            ):
                raise ValueError("finding references an unconsumed claim")
        scenario_hashes = {
            outcome.scenario_hash for outcome in self.obligation_outcomes
        }
        if len(scenario_hashes) != 1:
            raise ValueError("all findings must reference one scenario")
        if self.sensitivities[0].claim_id != (
            self.consumed_inputs.validation_inputs[0].claim_id
        ):
            raise ValueError("efficiency sensitivity must bind its validation claim")

        actual_counts = {
            "pass": sum(item.outcome == "pass" for item in self.obligation_outcomes),
            "fail": sum(item.outcome == "fail" for item in self.obligation_outcomes),
            "indeterminate": sum(
                item.outcome == "indeterminate" for item in self.obligation_outcomes
            ),
            "not_evaluated": sum(
                item.outcome == "not_evaluated" for item in self.obligation_outcomes
            ),
        }
        reported_counts = {
            "pass": self.coverage.counts.pass_count,
            "fail": self.coverage.counts.fail_count,
            "indeterminate": self.coverage.counts.indeterminate,
            "not_evaluated": self.coverage.counts.not_evaluated,
        }
        if actual_counts != reported_counts:
            raise ValueError("coverage counts must match obligation outcomes")
        return self


class StoredObjectReferenceV1(ExecutionContractV1):
    object_hash: HashId
    byte_length: Annotated[StrictInt, Field(gt=0, le=MAX_OBJECT_BYTES)]
    media_type: ShortIdentity
    schema_id: ShortIdentity
    schema_version: Literal["1.0.0", "2.0.0"]


class RunManifestV1(ExecutionContractV1):
    schema_id: Literal[MANIFEST_SCHEMA_ID] = Field(alias="$schema")
    schema_version: Literal["1.0.0"]
    manifest_kind: Literal["completed_analysis_run"]
    package: StoredObjectReferenceV1
    scenario: StoredObjectReferenceV1
    request: StoredObjectReferenceV1
    result: StoredObjectReferenceV1
    assembly_artifact_hash: HashId
    backend_id: Literal[BACKEND_ID]
    backend_contract_version: Literal[BACKEND_CONTRACT_VERSION]
    package_consumer_contract_hash: HashId
    numeric_profile: NumericProfileV1

    @model_validator(mode="after")
    def require_reference_types(self):
        actual = (
            (
                self.package.media_type,
                self.package.schema_id,
                self.package.schema_version,
            ),
            (
                self.scenario.media_type,
                self.scenario.schema_id,
                self.scenario.schema_version,
            ),
            (
                self.request.media_type,
                self.request.schema_id,
                self.request.schema_version,
            ),
            (
                self.result.media_type,
                self.result.schema_id,
                self.result.schema_version,
            ),
        )
        expected = (
            (PACKAGE_MEDIA_TYPE, PACKAGE_SCHEMA_ID, "2.0.0"),
            (SCENARIO_MEDIA_TYPE, SCENARIO_SCHEMA_ID, "1.0.0"),
            (REQUEST_MEDIA_TYPE, REQUEST_SCHEMA_ID, "1.0.0"),
            (RESULT_MEDIA_TYPE, RESULT_SCHEMA_ID, "1.0.0"),
        )
        if actual != expected:
            raise ValueError("manifest object references must use exact contract types")
        return self
