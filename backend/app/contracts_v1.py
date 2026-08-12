from typing import Annotated, Literal
from uuid import UUID

from pydantic import (
    AwareDatetime,
    AnyUrl,
    BaseModel,
    ConfigDict,
    Field,
    FiniteFloat,
    field_validator,
    model_validator,
)


NonEmptyString = Annotated[str, Field(min_length=1)]


class ContractModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class ResearchCreate(ContractModel):
    manufacturer: str = Field(min_length=1)
    part_number: str = Field(min_length=1)
    source_url: str | None = None


class ReviewDecision(ContractModel):
    field_name: str = Field(min_length=1)
    status: Literal["accepted", "rejected", "ambiguous"]
    note: str | None = None

    @model_validator(mode="after")
    def require_note_for_nonacceptance(self):
        if self.note is not None:
            self.note = self.note.strip() or None
        if self.status in {"rejected", "ambiguous"} and self.note is None:
            raise ValueError("rejected and ambiguous decisions require a note")
        return self


class ReviewRequest(ContractModel):
    reviewed_by: str = Field(min_length=1)
    decisions: list[ReviewDecision]

    @field_validator("reviewed_by")
    @classmethod
    def require_named_reviewer(cls, value: str) -> str:
        reviewer = value.strip()
        if not reviewer:
            raise ValueError("reviewed_by must contain a non-whitespace name")
        return reviewer


class ScalarValue(ContractModel):
    kind: Literal["scalar"]
    value: FiniteFloat


class RangeValue(ContractModel):
    kind: Literal["range"]
    minimum: FiniteFloat
    maximum: FiniteFloat

    @model_validator(mode="after")
    def require_ordered_bounds(self):
        if self.minimum > self.maximum:
            raise ValueError("range minimum must not exceed maximum")
        return self


class EnumerationValue(ContractModel):
    kind: Literal["enumeration"]
    values: list[str | FiniteFloat | bool] = Field(min_length=1)

    @model_validator(mode="after")
    def reject_duplicate_values(self):
        identities = [
            ("boolean", value)
            if isinstance(value, bool)
            else ("number", float(value))
            if isinstance(value, (int, float))
            else ("string", value)
            for value in self.values
        ]
        if len(identities) != len(set(identities)):
            raise ValueError("enumeration values must be unique")
        return self


class CurvePoint(ContractModel):
    x: FiniteFloat
    y: FiniteFloat


class CurveValue(ContractModel):
    kind: Literal["curve"]
    independent_quantity: str = Field(min_length=1)
    independent_unit: str = Field(min_length=1)
    interpolation: Literal["linear", "step", "cubic"]
    points: list[CurvePoint] = Field(min_length=2)

    @model_validator(mode="after")
    def require_increasing_x_coordinates(self):
        if any(
            current.x <= previous.x
            for previous, current in zip(self.points, self.points[1:])
        ):
            raise ValueError("curve x coordinates must be strictly increasing")
        return self


class UnknownValue(ContractModel):
    kind: Literal["unknown"]
    reason: str = Field(min_length=1)


EngineeringValue = Annotated[
    ScalarValue | RangeValue | EnumerationValue | CurveValue | UnknownValue,
    Field(discriminator="kind"),
]


class EvidenceReview(ContractModel):
    status: Literal["pending", "accepted", "rejected", "ambiguous"]
    reviewed_by: str | None
    reviewed_at: AwareDatetime | None
    note: str | None

    @model_validator(mode="after")
    def require_status_metadata(self):
        if self.status == "pending":
            if any(
                value is not None
                for value in (self.reviewed_by, self.reviewed_at, self.note)
            ):
                raise ValueError("pending evidence cannot contain review metadata")
        elif (
            not self.reviewed_by
            or not self.reviewed_by.strip()
            or self.reviewed_at is None
        ):
            raise ValueError("reviewed evidence requires reviewer and timestamp")
        elif self.status in {"rejected", "ambiguous"} and (
            not self.note or not self.note.strip()
        ):
            raise ValueError("rejected and ambiguous evidence requires a note")
        return self


class ExecutionEvidenceRecord(ContractModel):
    schema_version: Literal["1.0.0"]
    id: UUID
    evidence_class: Literal[
        "physically_validated",
        "system_validated",
        "manufacturer_stated",
        "supplier_stated",
        "user_measured",
        "user_provided",
        "derived",
        "llm_inferred",
        "synthetic_fixture",
        "unknown",
    ]
    source_document_id: UUID
    source_document_hash: str = Field(pattern=r"^sha256:[0-9a-f]{64}$")
    source_uri: AnyUrl
    source_locator: str = Field(min_length=1)
    excerpt: str = Field(min_length=1)
    confidence: FiniteFloat | None = Field(ge=0, le=1)
    extraction_method: str = Field(min_length=1)
    review: EvidenceReview


class ExecutionParameter(ContractModel):
    name: str = Field(min_length=1)
    quantity: str = Field(min_length=1)
    dimension: str = Field(min_length=1)
    value: EngineeringValue
    unit: str = Field(min_length=1)
    original_value: str = Field(min_length=1)
    original_unit: str = Field(min_length=1)
    validity_conditions: list[NonEmptyString]
    evidence_ids: list[UUID] = Field(min_length=1)

    @model_validator(mode="after")
    def reject_duplicate_evidence_references(self):
        if len(self.evidence_ids) != len(set(self.evidence_ids)):
            raise ValueError("parameter evidence IDs must be unique")
        return self


class ExecutionComponentIdentity(ContractModel):
    id: UUID
    manufacturer: str = Field(min_length=1)
    part_number: str = Field(min_length=1)
    revision: str = Field(min_length=1)
    component_class: str = Field(min_length=1)


class ExecutionCertification(ContractModel):
    tier: Literal[
        "provisional",
        "geometry_verified",
        "behavior_verified",
        "physically_validated",
        "system_validated",
    ]
    status: Literal["published"]
    published_at: AwareDatetime


class MissingInformation(ContractModel):
    field_name: str = Field(min_length=1)
    reason: str = Field(min_length=1)


class ExecutionAuthority(ContractModel):
    package_role: Literal["reviewed_input"]
    engineering_decision_authority: Literal["prometheus_cpp"]


class ExecutionComponentPayload(ContractModel):
    schema_version: Literal["1.0.0"]
    package_kind: Literal["component_execution_input"]
    revision_id: UUID
    component: ExecutionComponentIdentity
    certification: ExecutionCertification
    parameters: list[ExecutionParameter] = Field(min_length=1)
    evidence: list[ExecutionEvidenceRecord] = Field(min_length=1)
    supported_recipes: list[NonEmptyString]
    missing_information: list[MissingInformation]
    limitations: list[NonEmptyString]
    authority: ExecutionAuthority

    @model_validator(mode="after")
    def validate_graph_and_uniqueness(self):
        parameter_names = [parameter.name for parameter in self.parameters]
        if len(parameter_names) != len(set(parameter_names)):
            raise ValueError("parameter names must be unique")

        evidence_ids = [record.id for record in self.evidence]
        if len(evidence_ids) != len(set(evidence_ids)):
            raise ValueError("evidence IDs must be unique")
        known_evidence_ids = set(evidence_ids)
        absent_references = sorted(
            str(evidence_id)
            for parameter in self.parameters
            for evidence_id in parameter.evidence_ids
            if evidence_id not in known_evidence_ids
        )
        if absent_references:
            raise ValueError(
                f"parameter references absent evidence IDs: {absent_references}"
            )

        if len(self.supported_recipes) != len(set(self.supported_recipes)):
            raise ValueError("supported recipes must be unique")
        if self.certification.tier == "provisional" and not self.limitations:
            raise ValueError("provisional packages require at least one limitation")
        return self


class ExecutionComponentPackage(ExecutionComponentPayload):
    content_hash: str = Field(pattern=r"^sha256:[0-9a-f]{64}$")
