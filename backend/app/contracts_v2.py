"""Closed Program 01A v2 contracts.

These models validate transport and immutable package values.  They perform no
database I/O and deliberately contain no project-verdict derivation function.
"""

from __future__ import annotations

from datetime import datetime, timezone
import re
from typing import Annotated, Literal, TypeAlias
from uuid import UUID

from pydantic import (
    AnyUrl,
    BaseModel,
    BeforeValidator,
    ConfigDict,
    Field,
    PlainSerializer,
    StrictBool,
    StrictFloat,
    StrictInt,
    StrictStr,
    StringConstraints,
    TypeAdapter,
    WithJsonSchema,
    field_validator,
    model_validator,
)

from app.canonical_json import canonicalize_value


SCHEMA_ID = "urn:prometheus:schema:execution-component:2.0.0"
SCHEMA_VERSION = "2.0.0"
PACKAGE_MEDIA_TYPE = (
    "application/vnd.prometheus.execution-component+json;version=2.0.0"
)

EvidenceClass = Literal[
    "manufacturer_document",
    "private_upload",
    "user_measurement",
    "derived_claim",
    "validation_observation",
]
ReviewDecision = Literal["accepted", "rejected", "ambiguous"]
GatePhase = Literal["publication", "execution"]
GateState = Literal["pending", "satisfied", "blocked"]
SourceAuthority = Literal[
    "manufacturer",
    "supplier",
    "private_provider",
    "user",
    "prometheus_derivation",
    "validation_activity",
    "synthetic_fixture",
    "unknown",
]
PhysicalValidationStatus = Literal[
    "unvalidated", "component_validated", "system_validated"
]

HASH_PATTERN = r"^sha256:[0-9a-f]{64}$"
UUID4_PATTERN = (
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-"
    r"[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
)
EXACT_DECIMAL_PATTERN = r"^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?$"
SEMVER_PATTERN = r"^[0-9]+\.[0-9]+\.[0-9]+$"
ASCII_NAME_PATTERN = r"^[a-z][a-z0-9_]{0,127}$"
CAPABILITY_PATTERN = r"^[a-z][a-z0-9_.:-]{0,127}$"


class ContractV2(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)


def _validate_uuid4(value: object) -> UUID:
    if isinstance(value, UUID):
        parsed = value
    else:
        if type(value) is not str or re.fullmatch(UUID4_PATTERN, value) is None:
            raise ValueError("UUID must use lowercase hyphenated UUIDv4 spelling")
        parsed = UUID(value)
    if parsed.version != 4 or re.fullmatch(UUID4_PATTERN, str(parsed)) is None:
        raise ValueError("UUID must be version 4")
    return parsed


UuidV4 = Annotated[
    UUID,
    BeforeValidator(_validate_uuid4),
    PlainSerializer(lambda value: str(value), return_type=str),
    WithJsonSchema(
        {"type": "string", "format": "uuid", "pattern": UUID4_PATTERN}
    ),
]


def _validate_utc_datetime(value: object) -> datetime:
    if isinstance(value, datetime):
        parsed = value
    elif type(value) is str and value.endswith("Z"):
        try:
            parsed = datetime.fromisoformat(value[:-1] + "+00:00")
        except ValueError as exc:
            raise ValueError("timestamp must be RFC 3339 UTC") from exc
    else:
        raise ValueError("timestamp must be RFC 3339 UTC ending in Z")
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ValueError("timestamp must be timezone-aware UTC")
    if parsed.utcoffset().total_seconds() != 0:
        raise ValueError("timestamp must be UTC")
    return parsed.astimezone(timezone.utc)


UtcDatetime = Annotated[
    datetime,
    BeforeValidator(_validate_utc_datetime),
    PlainSerializer(
        lambda value: value.astimezone(timezone.utc)
        .isoformat()
        .replace("+00:00", "Z"),
        return_type=str,
    ),
    WithJsonSchema({"type": "string", "format": "date-time"}),
]

HashId = Annotated[StrictStr, Field(pattern=HASH_PATTERN)]
NonEmptyText = Annotated[
    StrictStr, StringConstraints(strip_whitespace=True, min_length=1, max_length=4096)
]
ShortText = Annotated[
    StrictStr, StringConstraints(strip_whitespace=True, min_length=1, max_length=512)
]
AsciiName = Annotated[StrictStr, Field(pattern=ASCII_NAME_PATTERN)]
CapabilityId = Annotated[StrictStr, Field(pattern=CAPABILITY_PATTERN)]
VersionText = Annotated[StrictStr, Field(pattern=SEMVER_PATTERN)]
NonNegativeInt = Annotated[StrictInt, Field(ge=0)]


_URL_ADAPTER = TypeAdapter(AnyUrl)


def _validate_source_uri(value: object) -> str:
    if type(value) is not str:
        raise ValueError("source URI must be a string")
    _URL_ADAPTER.validate_python(value)
    return value


SourceUri = Annotated[
    StrictStr,
    BeforeValidator(_validate_source_uri),
    WithJsonSchema({"type": "string", "format": "uri"}),
]


def _validate_json_number(value: object) -> int | float:
    if type(value) not in (int, float):
        raise ValueError("JSON number must be an uncoerced integer or float")
    canonicalize_value(value)
    return value


JsonNumber = Annotated[
    StrictInt | StrictFloat,
    BeforeValidator(_validate_json_number),
]
ConfidenceNumber = Annotated[JsonNumber, Field(ge=0, le=1)]


def _trim_bounded_text(value: object, *, field: str, maximum: int) -> str:
    if type(value) is not str:
        raise ValueError(f"{field} must be a string")
    trimmed = value.strip()
    byte_length = len(trimmed.encode("utf-8"))
    if not 1 <= byte_length <= maximum:
        raise ValueError(f"{field} must be 1 to {maximum} UTF-8 bytes after trimming")
    return trimmed


def _require_sorted_unique(values: list[object], key, field: str) -> None:
    identities = [key(value) for value in values]
    if identities != sorted(identities):
        raise ValueError(f"{field} must use canonical contract order")
    if len(identities) != len(set(identities)):
        raise ValueError(f"{field} must not contain duplicate identities")


class ScalarValueV2(ContractV2):
    kind: Literal["scalar"]
    value: JsonNumber


class RangeValueV2(ContractV2):
    kind: Literal["range"]
    minimum: JsonNumber
    maximum: JsonNumber

    @model_validator(mode="after")
    def require_ordered_bounds(self):
        if self.minimum > self.maximum:
            raise ValueError("range minimum must not exceed maximum")
        return self


EnumerationScalar: TypeAlias = StrictStr | JsonNumber | StrictBool


class EnumerationValueV2(ContractV2):
    kind: Literal["enumeration"]
    values: list[EnumerationScalar] = Field(min_length=1, max_length=10_000)

    @model_validator(mode="after")
    def require_distinct_values(self):
        identities: list[tuple[str, object]] = []
        for value in self.values:
            if type(value) is bool:
                identities.append(("boolean", value))
            elif type(value) is str:
                identities.append(("string", value))
            else:
                identities.append(("number", canonicalize_value(value)))
        if len(identities) != len(set(identities)):
            raise ValueError("enumeration values must be unique")
        return self


class CurvePointV2(ContractV2):
    x: JsonNumber
    y: JsonNumber


class CurveValueV2(ContractV2):
    kind: Literal["curve"]
    independent_quantity: AsciiName
    independent_unit: ShortText
    interpolation: Literal["linear", "step", "cubic"]
    points: list[CurvePointV2] = Field(min_length=2, max_length=10_000)

    @model_validator(mode="after")
    def require_increasing_x(self):
        if any(
            current.x <= previous.x
            for previous, current in zip(self.points, self.points[1:])
        ):
            raise ValueError("curve x coordinates must be strictly increasing")
        return self


class ExactDecimalStringValueV2(ContractV2):
    kind: Literal["exact_decimal_string"]
    value: Annotated[StrictStr, Field(pattern=EXACT_DECIMAL_PATTERN, max_length=4096)]


class UnknownValueV2(ContractV2):
    kind: Literal["unknown"]
    reason: NonEmptyText


KnownEngineeringValueV2: TypeAlias = Annotated[
    ScalarValueV2
    | RangeValueV2
    | EnumerationValueV2
    | CurveValueV2
    | ExactDecimalStringValueV2,
    Field(discriminator="kind"),
]
EngineeringValueV2: TypeAlias = Annotated[
    ScalarValueV2
    | RangeValueV2
    | EnumerationValueV2
    | CurveValueV2
    | ExactDecimalStringValueV2
    | UnknownValueV2,
    Field(discriminator="kind"),
]


class CandidateClaimBaseV2(ContractV2):
    claim_id: UuidV4
    revision_id: UuidV4
    slot_id: UuidV4
    validity_conditions: list[NonEmptyText] = Field(max_length=1_000)
    provenance: ShortText
    evidence_ids: list[UuidV4] = Field(max_length=10_000)
    claim_fingerprint: HashId

    @model_validator(mode="after")
    def require_ordered_references(self):
        _require_sorted_unique(
            self.evidence_ids, lambda value: str(value), "claim evidence IDs"
        )
        if len(self.validity_conditions) != len(set(self.validity_conditions)):
            raise ValueError("validity conditions must be unique")
        return self


class KnownCandidateClaimV2(CandidateClaimBaseV2):
    value_state: Literal["known"]
    value: KnownEngineeringValueV2
    unit: ShortText
    original_value: NonEmptyText
    original_unit: ShortText


class UnknownCandidateClaimV2(CandidateClaimBaseV2):
    value_state: Literal["unknown"]
    value: UnknownValueV2


CandidateClaimV2: TypeAlias = Annotated[
    KnownCandidateClaimV2 | UnknownCandidateClaimV2,
    Field(discriminator="value_state"),
]


class EvidenceBaseV2(ContractV2):
    evidence_id: UuidV4
    revision_id: UuidV4
    evidence_class: EvidenceClass
    source_authority: SourceAuthority
    physical_validation_status: PhysicalValidationStatus
    extraction_confidence: ConfidenceNumber | None = None
    source_uri: SourceUri | None = None
    source_locator: ShortText | None = None
    excerpt: NonEmptyText | None = None
    page: Annotated[StrictInt, Field(ge=1)] | None = None
    limitations: list[NonEmptyText] = Field(max_length=1_000)

    @model_validator(mode="after")
    def require_consistent_document_locator(self):
        if (self.source_locator is None) != (self.excerpt is None):
            raise ValueError("source locator and excerpt must be supplied together")
        if self.page is not None and self.source_locator is None:
            raise ValueError("page requires a source locator and excerpt")
        if len(self.limitations) != len(set(self.limitations)):
            raise ValueError("evidence limitations must be unique")
        return self


class ManufacturerDocumentEvidenceV2(EvidenceBaseV2):
    evidence_class: Literal["manufacturer_document"]
    source_authority: Literal["manufacturer", "supplier"]
    artifact_hash: HashId
    document_identity: ShortText


class PrivateUploadEvidenceV2(EvidenceBaseV2):
    evidence_class: Literal["private_upload"]
    source_authority: Literal["private_provider", "synthetic_fixture", "user"]
    artifact_hash: HashId
    local_provenance: NonEmptyText


class UserMeasurementEvidenceV2(EvidenceBaseV2):
    evidence_class: Literal["user_measurement"]
    source_authority: Literal["user"]
    measurement_method: NonEmptyText
    measurement_unit: ShortText
    observed_at: UtcDatetime
    artifact_hash: HashId | None = None
    recorded_observation: NonEmptyText | None = None

    @model_validator(mode="after")
    def require_measurement_record(self):
        if self.artifact_hash is None and self.recorded_observation is None:
            raise ValueError(
                "measurement requires an artifact or a recorded observation"
            )
        return self


class DerivedClaimEvidenceV2(EvidenceBaseV2):
    evidence_class: Literal["derived_claim"]
    source_authority: Literal["prometheus_derivation"]
    derivation_method: NonEmptyText
    parent_claim_ids: list[UuidV4] = Field(max_length=10_000)
    parent_evidence_ids: list[UuidV4] = Field(max_length=10_000)

    @model_validator(mode="after")
    def require_ordered_parent_references(self):
        if not self.parent_claim_ids and not self.parent_evidence_ids:
            raise ValueError("derived evidence requires at least one parent")
        _require_sorted_unique(
            self.parent_claim_ids,
            lambda value: str(value),
            "derived parent claim IDs",
        )
        _require_sorted_unique(
            self.parent_evidence_ids,
            lambda value: str(value),
            "derived parent evidence IDs",
        )
        return self


class ValidationObservationEvidenceV2(EvidenceBaseV2):
    evidence_class: Literal["validation_observation"]
    source_authority: Literal["validation_activity", "user"]
    test_provenance: NonEmptyText
    observed_at: UtcDatetime
    recorded_observation: NonEmptyText
    artifact_hash: HashId | None = None


EvidenceRecordV2: TypeAlias = Annotated[
    ManufacturerDocumentEvidenceV2
    | PrivateUploadEvidenceV2
    | UserMeasurementEvidenceV2
    | DerivedClaimEvidenceV2
    | ValidationObservationEvidenceV2,
    Field(discriminator="evidence_class"),
]


class ClaimReviewDecisionV2(ContractV2):
    claim_id: UuidV4
    status: ReviewDecision
    note: Annotated[StrictStr, Field(min_length=1, max_length=4096)]

    @field_validator("note", mode="before")
    @classmethod
    def validate_note(cls, value: object) -> str:
        return _trim_bounded_text(value, field="note", maximum=4096)


class ReviewRequestV2(ContractV2):
    expected_draft_version: NonNegativeInt
    reviewed_by: Annotated[StrictStr, Field(min_length=1, max_length=256)]
    decisions: list[ClaimReviewDecisionV2] = Field(min_length=1, max_length=1000)

    @field_validator("reviewed_by", mode="before")
    @classmethod
    def validate_reviewer(cls, value: object) -> str:
        return _trim_bounded_text(value, field="reviewed_by", maximum=256)

    @model_validator(mode="after")
    def reject_duplicate_claims(self):
        claim_ids = [decision.claim_id for decision in self.decisions]
        if len(claim_ids) != len(set(claim_ids)):
            raise ValueError("a review batch may name each claim only once")
        return self


class PublicationRequestV2(ContractV2):
    expected_draft_version: NonNegativeInt
    schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"]
    schema_version: Literal["2.0.0"]


class PackageCompilerV2(ContractV2):
    name: Literal["prometheus_python"]
    version: VersionText


class ComponentIdentityV2(ContractV2):
    component_id: UuidV4
    manufacturer: ShortText
    part_number: ShortText
    revision: ShortText
    component_class: AsciiName


class ArtifactReferenceV2(ContractV2):
    artifact_hash: HashId
    media_type: ShortText
    byte_length: NonNegativeInt
    filename: ShortText
    artifact_role: Literal["source_evidence", "supporting_input"]


class ParameterSlotV2(ContractV2):
    slot_id: UuidV4
    name: AsciiName
    quantity: AsciiName
    dimension: ShortText
    required_for_execution: StrictBool
    selected_claim_id: UuidV4


class EffectiveClaimReviewV2(ContractV2):
    review_event_id: UuidV4
    revision_id: UuidV4
    claim_id: UuidV4
    reviewed_claim_fingerprint: HashId
    decision: Literal["accepted"]
    reviewed_by: Annotated[StrictStr, Field(min_length=1, max_length=256)]
    note: Annotated[StrictStr, Field(min_length=1, max_length=4096)]
    reviewed_at: UtcDatetime
    applied_draft_version: Annotated[StrictInt, Field(ge=1)]

    @field_validator("reviewed_by", mode="before")
    @classmethod
    def validate_reviewer(cls, value: object) -> str:
        return _trim_bounded_text(value, field="reviewed_by", maximum=256)

    @field_validator("note", mode="before")
    @classmethod
    def validate_note(cls, value: object) -> str:
        return _trim_bounded_text(value, field="note", maximum=4096)


RequiredReviewType = Literal[
    "component_identity",
    "source_artifact",
    "claim_selection",
    "claim_review",
    "package_consumer",
]


class CapabilityGateV2(ContractV2):
    gate_id: UuidV4
    capability_id: CapabilityId
    phase: GatePhase
    required_review_type: RequiredReviewType
    state: GateState
    satisfying_reference_ids: list[StrictStr] = Field(max_length=10_000)
    reason: NonEmptyText | None = None

    @model_validator(mode="after")
    def require_state_metadata(self):
        if self.satisfying_reference_ids != sorted(self.satisfying_reference_ids):
            raise ValueError("gate satisfying references must use ASCII order")
        if len(self.satisfying_reference_ids) != len(
            set(self.satisfying_reference_ids)
        ):
            raise ValueError("gate satisfying references must be unique")
        if self.state == "satisfied":
            if not self.satisfying_reference_ids or self.reason is not None:
                raise ValueError("satisfied gates require references and no reason")
        elif self.satisfying_reference_ids or self.reason is None:
            raise ValueError("pending or blocked gates require only an explicit reason")
        return self


class MissingInformationV2(ContractV2):
    missing_information_id: UuidV4
    slot_id: UuidV4 | None = None
    reason: NonEmptyText
    unlocks_capability_ids: list[CapabilityId] = Field(max_length=1_000)

    @model_validator(mode="after")
    def require_ordered_capabilities(self):
        if self.unlocks_capability_ids != sorted(self.unlocks_capability_ids):
            raise ValueError("unlock capability IDs must use ASCII order")
        if len(self.unlocks_capability_ids) != len(set(self.unlocks_capability_ids)):
            raise ValueError("unlock capability IDs must be unique")
        return self


class LimitationV2(ContractV2):
    limitation_id: UuidV4
    statement: NonEmptyText


class ExecutionAuthorityV2(ContractV2):
    package_role: Literal["reviewed_input"]
    engineering_decision_authority: Literal["prometheus_cpp"]
    authority_role: Literal["input_only"]


class ExecutionComponentV2(ContractV2):
    schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"] = Field(
        alias="$schema"
    )
    schema_version: Literal["2.0.0"]
    package_kind: Literal["component_execution_input"]
    package_compiler: PackageCompilerV2
    revision_id: UuidV4
    reviewed_draft_version: Annotated[StrictInt, Field(ge=1)]
    component: ComponentIdentityV2
    capability_id: CapabilityId
    artifacts: list[ArtifactReferenceV2] = Field(min_length=1, max_length=10_000)
    parameter_slots: list[ParameterSlotV2] = Field(
        min_length=1, max_length=10_000
    )
    claims: list[CandidateClaimV2] = Field(min_length=1, max_length=10_000)
    evidence: list[EvidenceRecordV2] = Field(max_length=10_000)
    claim_reviews: list[EffectiveClaimReviewV2] = Field(
        min_length=1, max_length=10_000
    )
    gates: list[CapabilityGateV2] = Field(min_length=1, max_length=10_000)
    execution_readiness: Literal["ready", "blocked"]
    missing_information: list[MissingInformationV2] = Field(max_length=10_000)
    limitations: list[LimitationV2] = Field(min_length=1, max_length=10_000)
    authority: ExecutionAuthorityV2

    @model_validator(mode="after")
    def validate_order_and_graph(self):
        _require_sorted_unique(
            self.artifacts,
            lambda item: item.artifact_hash,
            "artifacts",
        )
        _require_sorted_unique(
            self.parameter_slots,
            lambda item: (item.name, str(item.slot_id)),
            "parameter slots",
        )
        _require_sorted_unique(
            self.claims, lambda item: str(item.claim_id), "claims"
        )
        _require_sorted_unique(
            self.evidence, lambda item: str(item.evidence_id), "evidence"
        )
        _require_sorted_unique(
            self.claim_reviews,
            lambda item: str(item.review_event_id),
            "claim reviews",
        )
        _require_sorted_unique(
            self.gates, lambda item: str(item.gate_id), "gates"
        )
        _require_sorted_unique(
            self.missing_information,
            lambda item: str(item.missing_information_id),
            "missing information",
        )
        _require_sorted_unique(
            self.limitations,
            lambda item: str(item.limitation_id),
            "limitations",
        )

        revision_id = self.revision_id
        claims = {claim.claim_id: claim for claim in self.claims}
        slots = {slot.slot_id: slot for slot in self.parameter_slots}
        evidence = {record.evidence_id: record for record in self.evidence}
        artifacts = {artifact.artifact_hash for artifact in self.artifacts}
        reviews = {review.claim_id: review for review in self.claim_reviews}

        selected_claim_ids = {slot.selected_claim_id for slot in self.parameter_slots}
        if selected_claim_ids != set(claims):
            raise ValueError("package must contain exactly one selected claim per slot")
        if len(selected_claim_ids) != len(self.parameter_slots):
            raise ValueError("one claim cannot satisfy multiple parameter slots")

        for slot in self.parameter_slots:
            claim = claims[slot.selected_claim_id]
            if claim.slot_id != slot.slot_id or claim.revision_id != revision_id:
                raise ValueError("slot selection must reference a same-revision claim")

        if set(reviews) != set(claims):
            raise ValueError("every selected claim requires one effective accepted review")
        if len(reviews) != len(self.claim_reviews):
            raise ValueError("a selected claim may have only one effective review")

        for claim in self.claims:
            if claim.revision_id != revision_id or claim.slot_id not in slots:
                raise ValueError("claims must belong to this package revision and slot")
            if any(evidence_id not in evidence for evidence_id in claim.evidence_ids):
                raise ValueError("claim references evidence absent from the package")
            review = reviews[claim.claim_id]
            if (
                review.revision_id != revision_id
                or review.reviewed_claim_fingerprint != claim.claim_fingerprint
                or review.applied_draft_version > self.reviewed_draft_version
            ):
                raise ValueError("effective review does not match the selected claim")

        for record in self.evidence:
            if record.revision_id != revision_id:
                raise ValueError("evidence must belong to this package revision")
            artifact_hash = getattr(record, "artifact_hash", None)
            if artifact_hash is not None and artifact_hash not in artifacts:
                raise ValueError("evidence references an absent artifact")
            if isinstance(record, DerivedClaimEvidenceV2):
                if any(parent not in claims for parent in record.parent_claim_ids):
                    raise ValueError("derived evidence references an absent claim")
                if any(parent not in evidence for parent in record.parent_evidence_ids):
                    raise ValueError("derived evidence references absent evidence")

        known_references = (
            {str(self.component.component_id)}
            | artifacts
            | {str(claim_id) for claim_id in claims}
            | {str(review.review_event_id) for review in self.claim_reviews}
        )
        publication_gates = []
        execution_gates = []
        for gate in self.gates:
            if gate.capability_id != self.capability_id:
                raise ValueError("package gates must belong to the selected capability")
            if any(
                reference not in known_references
                for reference in gate.satisfying_reference_ids
            ):
                raise ValueError("gate references an absent package identity")
            if gate.phase == "publication":
                publication_gates.append(gate)
                if gate.state != "satisfied":
                    raise ValueError("all publication gates must be satisfied")
            else:
                execution_gates.append(gate)

        if not publication_gates:
            raise ValueError("package requires at least one publication gate")
        expected_readiness = (
            "ready"
            if all(gate.state == "satisfied" for gate in execution_gates)
            else "blocked"
        )
        if self.execution_readiness != expected_readiness:
            raise ValueError("execution readiness does not match execution gates")

        for item in self.missing_information:
            if item.slot_id is not None and item.slot_id not in slots:
                raise ValueError("missing information references an absent slot")
        return self


class ProjectCountsV2(ContractV2):
    satisfied_within_scope: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]
    violated: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]
    indeterminate: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]
    not_applicable: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]
    not_evaluated: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]


class DecisionCoreIdentityV2(ContractV2):
    name: Literal["prometheus_cpp"]
    version: VersionText


Verdict = Literal[
    "satisfied_within_scope", "requirements_violated", "indeterminate"
]
Coverage = Literal["sufficient", "insufficient", "not_assessed"]
ExecutionState = Literal[
    "not_started",
    "ready",
    "running",
    "blocked",
    "completed",
    "completed_with_blocked_work",
    "failed",
    "cancelled",
]


class ProjectSummaryV2(ContractV2):
    schema_version: Literal["2.0.0"]
    verdict: Verdict
    coverage: Coverage
    execution_state: ExecutionState
    counts: ProjectCountsV2
    obligation_total: Annotated[StrictInt, Field(ge=0, le=2**64 - 1)]
    assessment_scope_id: HashId
    decision_core: DecisionCoreIdentityV2

    @model_validator(mode="after")
    def validate_consistency(self):
        counts = self.counts
        total = (
            counts.satisfied_within_scope
            + counts.violated
            + counts.indeterminate
            + counts.not_applicable
            + counts.not_evaluated
        )
        if total != self.obligation_total:
            raise ValueError("obligation counts must sum to obligation_total")

        unresolved = counts.indeterminate + counts.not_evaluated
        applicable = (
            counts.satisfied_within_scope + counts.violated + unresolved
        )
        if applicable == 0:
            expected_coverage = "not_assessed"
        elif unresolved:
            expected_coverage = "insufficient"
        else:
            expected_coverage = "sufficient"
        if self.coverage != expected_coverage:
            raise ValueError("coverage is inconsistent with obligation counts")

        if counts.violated:
            expected_verdict = "requirements_violated"
        elif unresolved or applicable == 0 or self.execution_state != "completed":
            expected_verdict = "indeterminate"
        else:
            expected_verdict = "satisfied_within_scope"
        if self.verdict != expected_verdict:
            raise ValueError("verdict is inconsistent with counts and execution state")
        return self


__all__ = [
    "PACKAGE_MEDIA_TYPE",
    "SCHEMA_ID",
    "SCHEMA_VERSION",
    "CandidateClaimV2",
    "CapabilityGateV2",
    "ClaimReviewDecisionV2",
    "ContractV2",
    "EngineeringValueV2",
    "EvidenceRecordV2",
    "ExecutionComponentV2",
    "ProjectSummaryV2",
    "PublicationRequestV2",
    "ReviewRequestV2",
]
