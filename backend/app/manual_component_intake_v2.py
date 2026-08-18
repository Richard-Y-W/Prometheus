"""Manually entered non-catalog component draft intake (Phase 5, checkpoint 1).

This builds the same Program 01A v2 draft graph shape produced by
`fixture_pipeline_v2.py`, but from a live typed submission instead of a
pinned local fixture file. Every manually entered value is retained as
`user_measurement` evidence with an explicit measurement method and
timestamp, and the exact canonical submission is stored as an immutable
artifact so the source of every claim is retained even when no external
document was attached. Unknown values retain a reason and no evidence,
matching the fixture pipeline's known/unknown split.

This intake path never marks a claim reviewed or publishable on its own:
the `claim_review` gate always starts `pending`. The `package_consumer`
execution gate starts `satisfied` only when the submitted `capability_id`
names a capability with a real, versioned C++ consumer (see
`_MANUALLY_CONSUMABLE_CAPABILITIES`, Phase 5 checkpoint 4); every other
capability starts `blocked`, because no Prometheus capability consumes it.
A satisfied gate is a claim that a consumer exists for the capability, not
that this submission's specific values will pass -- the C++ decision layer
still independently determines execution readiness from the actual bound
claims. Review, publication, and export reuse the existing
`review_service_v2` / `publication_service_v2` routes unchanged.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Annotated

from pydantic import Field, StrictStr, model_validator
import sqlalchemy as sa
from sqlalchemy.orm import Session

from .artifact_store import ingest_local_artifact, store_submitted_artifact
from .canonical_json import canonicalize_value, object_hash
from .claim_identity_v2 import fingerprint_claim_fields
from .fixture_catalog_v2 import (
    CONSUMER_HASH,
    CONSUMER_MEDIA_TYPE,
    CONSUMER_PATH,
    DC_GEARMOTOR_CAPABILITY,
)
from .contracts_v2 import (
    SCHEMA_ID,
    SCHEMA_VERSION,
    AsciiName,
    CapabilityId,
    ContractV2,
    EngineeringValueV2,
    NonEmptyText,
    ShortText,
    UtcDatetime,
)
from .models import uid
from .models_v1 import Component, ComponentRevision, Manufacturer
from .models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimEvidenceLinkV2,
    ClaimSelectionV2,
    EvidenceRecordV2,
    ManualComponentDraftJobV2,
    ParameterSlotV2,
)


_IDEMPOTENCY_PATTERN = re.compile(r"[A-Za-z0-9._:-]{16,128}\Z")
_SchemaVersionText = Annotated[StrictStr, Field(min_length=1, max_length=32)]

MANUAL_ENTRY_RECORD_MEDIA_TYPE = (
    "application/vnd.prometheus.manual-component-entry+json;version=1.0.0"
)
MANUAL_ENTRY_LIMITATION = (
    "This component's specifications were manually typed by a user and have "
    "not been independently verified against a manufacturer source."
)
NO_CONSUMER_REASON = (
    "No Prometheus execution capability consumes manually entered "
    "components yet."
)

# Capabilities with a real, versioned C++ consumer that a manually entered
# component may also declare. This mirrors FixtureDefinition.consumer_hash
# in fixture_catalog_v2.py: naming a capability here is a claim that a
# compiled consumer exists for it, not that any specific submission's
# parameters will satisfy that consumer -- the C++ decision layer still
# independently determines execution readiness from the actual bound
# values, exactly as it already does for the Motor A/B fixtures.
_MANUALLY_CONSUMABLE_CAPABILITIES: dict[str, str] = {
    DC_GEARMOTOR_CAPABILITY: CONSUMER_HASH,
}


def _package_consumer_gate(
    db: Session, revision_id: str, capability_id: str
) -> CapabilityGateV2:
    consumer_hash = _MANUALLY_CONSUMABLE_CAPABILITIES.get(capability_id)
    if consumer_hash is None:
        return CapabilityGateV2(
            revision_id=revision_id,
            capability_id=capability_id,
            phase="execution",
            required_review_type="package_consumer",
            state="blocked",
            satisfying_references=[],
            reason=NO_CONSUMER_REASON,
        )
    # The package compiler binds every satisfied package_consumer reference
    # to a real ArtifactObjectV2 row (the versioned consumer-contract
    # document), exactly as fixture ingestion already does for Motor A/B.
    # This idempotently installs that same well-known artifact so a manual
    # draft's compiled package carries the identical supporting_input the
    # fixture path already proved works, rather than a dangling reference.
    ingest_local_artifact(
        db,
        source_path=CONSUMER_PATH,
        allowed_root=CONSUMER_PATH.parent,
        expected_hash=consumer_hash,
        media_type=CONSUMER_MEDIA_TYPE,
    )
    return CapabilityGateV2(
        revision_id=revision_id,
        capability_id=capability_id,
        phase="execution",
        required_review_type="package_consumer",
        state="satisfied",
        satisfying_references=[consumer_hash],
        reason=None,
    )


def _normalize_identity(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


class ManualComponentDraftError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class _KnownManualParameter(ContractV2):
    name: AsciiName
    quantity: AsciiName
    dimension: ShortText
    required_for_execution: bool = False
    value: EngineeringValueV2
    unit: ShortText
    original_value: NonEmptyText
    original_unit: ShortText
    validity_conditions: list[NonEmptyText] = Field(
        default_factory=list, max_length=1000
    )
    measurement_method: NonEmptyText
    observed_at: UtcDatetime

    @model_validator(mode="after")
    def require_known_value_and_unique_conditions(self):
        if self.value.kind == "unknown":
            raise ValueError(
                "a known manual parameter cannot contain an unknown value"
            )
        if len(self.validity_conditions) != len(set(self.validity_conditions)):
            raise ValueError("validity conditions must be unique")
        return self


class _UnknownManualParameter(ContractV2):
    name: AsciiName
    quantity: AsciiName
    dimension: ShortText
    required_for_execution: bool = False
    unknown_reason: NonEmptyText
    validity_conditions: list[NonEmptyText] = Field(
        default_factory=list, max_length=1000
    )

    @model_validator(mode="after")
    def require_unique_conditions(self):
        if len(self.validity_conditions) != len(set(self.validity_conditions)):
            raise ValueError("validity conditions must be unique")
        return self


_ManualParameter = _KnownManualParameter | _UnknownManualParameter


class ManualComponentDraftRequestV2(ContractV2):
    schema_version: _SchemaVersionText
    manufacturer: ShortText
    part_number: ShortText
    component_class: AsciiName
    revision: ShortText
    capability_id: CapabilityId
    limitations: list[NonEmptyText] = Field(default_factory=list, max_length=1000)
    parameters: list[_ManualParameter] = Field(min_length=1, max_length=1000)

    @model_validator(mode="after")
    def require_unique_names_and_limitations(self):
        names = [parameter.name for parameter in self.parameters]
        if len(names) != len(set(names)):
            raise ValueError("parameter names must be unique")
        if len(self.limitations) != len(set(self.limitations)):
            raise ValueError("limitations must be unique")
        return self


@dataclass(frozen=True)
class ManualComponentDraftResult:
    job: ManualComponentDraftJobV2
    revision: ComponentRevision
    slots: tuple[ParameterSlotV2, ...]
    claims: tuple[CandidateClaimV2, ...]
    selections: tuple[ClaimSelectionV2, ...]
    gates: tuple[CapabilityGateV2, ...]


def _request_fingerprint(canonical_request: bytes) -> str:
    return object_hash(canonical_request)


def _claim_fingerprint(
    *,
    revision_id: str,
    slot_id: str,
    value_state: str,
    value: dict[str, object],
    provenance: str,
    evidence_ids: list[str],
    validity_conditions: list[str],
    unit: str | None,
    original_value: str | None,
    original_unit: str | None,
) -> str:
    return fingerprint_claim_fields(
        revision_id=revision_id,
        slot_id=slot_id,
        value_state=value_state,
        value=value,
        provenance=provenance,
        evidence_ids=evidence_ids,
        validity_conditions=validity_conditions,
        unit=unit,
        original_value=original_value,
        original_unit=original_unit,
    )


def _ordered_graph(
    db: Session,
    job: ManualComponentDraftJobV2,
    revision: ComponentRevision,
) -> ManualComponentDraftResult:
    slots = tuple(
        sorted(
            db.scalars(
                sa.select(ParameterSlotV2).where(
                    ParameterSlotV2.revision_id == revision.id
                )
            ),
            key=lambda slot: (slot.name, slot.id),
        )
    )
    slot_order = {slot.id: index for index, slot in enumerate(slots)}
    claims = tuple(
        sorted(
            db.scalars(
                sa.select(CandidateClaimV2).where(
                    CandidateClaimV2.revision_id == revision.id
                )
            ),
            key=lambda claim: (slot_order[claim.slot_id], claim.id),
        )
    )
    selections = tuple(
        sorted(
            db.scalars(
                sa.select(ClaimSelectionV2).where(
                    ClaimSelectionV2.revision_id == revision.id
                )
            ),
            key=lambda selection: (slot_order[selection.slot_id], selection.id),
        )
    )
    gate_order = {
        "component_identity": 0,
        "source_artifact": 1,
        "claim_selection": 2,
        "claim_review": 3,
        "package_consumer": 4,
    }
    gates = tuple(
        sorted(
            db.scalars(
                sa.select(CapabilityGateV2).where(
                    CapabilityGateV2.revision_id == revision.id
                )
            ),
            key=lambda gate: (gate_order[gate.required_review_type], gate.id),
        )
    )
    return ManualComponentDraftResult(job, revision, slots, claims, selections, gates)


def _existing_result(
    db: Session,
    job: ManualComponentDraftJobV2,
    canonical_request: bytes,
) -> ManualComponentDraftResult:
    if job.request_fingerprint != _request_fingerprint(canonical_request):
        raise ManualComponentDraftError(
            "manual_draft_idempotency_conflict",
            "the idempotency key is already bound to a different manual "
            "component draft request",
        )
    if job.status != "succeeded" or job.revision_id is None:
        raise ManualComponentDraftError(
            "manual_draft_ingestion_incomplete",
            "the manual component draft idempotency record is not a "
            "completed draft",
        )
    revision = db.get(ComponentRevision, job.revision_id)
    if revision is None:
        raise ManualComponentDraftError(
            "manual_draft_integrity_failure",
            "the completed manual component draft job references a "
            "missing revision",
        )
    return _ordered_graph(db, job, revision)


def _create_graph(
    db: Session,
    *,
    request: ManualComponentDraftRequestV2,
    idempotency_key: str,
    canonical_request: bytes,
) -> ManualComponentDraftResult:
    job = ManualComponentDraftJobV2(
        idempotency_key=idempotency_key,
        request_fingerprint=_request_fingerprint(canonical_request),
        status="queued",
        revision_id=None,
        artifact_hash=None,
        error_code=None,
        error_message=None,
    )
    db.add(job)
    db.flush()

    artifact = store_submitted_artifact(
        db,
        payload_bytes=canonical_request,
        media_type=MANUAL_ENTRY_RECORD_MEDIA_TYPE,
        filename=f"manual-component-entry-{job.id}.json",
    )
    job.status = "running"
    db.flush()

    manufacturer = db.scalar(
        sa.select(Manufacturer).where(Manufacturer.name == request.manufacturer)
    )
    if manufacturer is None:
        manufacturer = Manufacturer(
            name=request.manufacturer,
            normalized_name=_normalize_identity(request.manufacturer),
        )
        db.add(manufacturer)
        db.flush()
    elif manufacturer.normalized_name != _normalize_identity(request.manufacturer):
        raise ManualComponentDraftError(
            "manual_draft_identity_conflict",
            "stored manufacturer identity conflicts with the submitted name",
        )

    component = db.scalar(
        sa.select(Component).where(
            Component.manufacturer_id == manufacturer.id,
            Component.part_number == request.part_number,
        )
    )
    if component is None:
        component = Component(
            manufacturer_id=manufacturer.id,
            part_number=request.part_number,
            normalized_part_number=_normalize_identity(request.part_number),
            family="manually entered component",
            model_class=request.component_class,
        )
        db.add(component)
        db.flush()
    elif (
        component.normalized_part_number != _normalize_identity(request.part_number)
        or component.model_class != request.component_class
    ):
        raise ManualComponentDraftError(
            "manual_draft_identity_conflict",
            "stored component identity conflicts with the submitted part",
        )

    if db.scalar(
        sa.select(ComponentRevision.id).where(
            ComponentRevision.component_id == component.id,
            ComponentRevision.revision == request.revision,
        )
    ) is not None:
        raise ManualComponentDraftError(
            "manual_draft_revision_exists",
            "the exact component revision already exists under another request",
        )

    unknown_parameters = [
        parameter
        for parameter in request.parameters
        if isinstance(parameter, _UnknownManualParameter)
    ]
    revision = ComponentRevision(
        component_id=component.id,
        revision=request.revision,
        parent_revision_id=None,
        certification_tier="provisional",
        status="draft",
        published_at=None,
        content_hash=None,
        draft_version=0,
        contract_schema_id=SCHEMA_ID,
        contract_schema_version=SCHEMA_VERSION,
        publication_integrity="v2_draft",
        published_object_hash=None,
        supported_recipes=[request.capability_id],
        missing_information=[],
        limitations=[],
    )
    db.add(revision)
    db.flush()

    ordered_parameters = sorted(request.parameters, key=lambda item: item.name)
    slots = [
        ParameterSlotV2(
            revision_id=revision.id,
            name=parameter.name,
            quantity=parameter.quantity,
            dimension=parameter.dimension,
            required_for_execution=parameter.required_for_execution,
        )
        for parameter in ordered_parameters
    ]
    db.add_all(slots)
    db.flush()
    slot_by_name = {slot.name: slot for slot in slots}
    revision.missing_information = [
        {
            "missing_information_id": uid(),
            "slot_id": slot_by_name[parameter.name].id,
            "reason": parameter.unknown_reason,
            "unlocks_capability_ids": [],
        }
        for parameter in unknown_parameters
    ]
    revision.limitations = [
        {"limitation_id": uid(), "statement": statement}
        for statement in (*request.limitations, MANUAL_ENTRY_LIMITATION)
    ]
    db.flush()

    claims: list[CandidateClaimV2] = []
    selections: list[ClaimSelectionV2] = []
    for parameter, slot in zip(ordered_parameters, slots, strict=True):
        evidence_ids: list[str] = []
        if isinstance(parameter, _KnownManualParameter):
            recorded_observation = (
                f"{parameter.name}: {parameter.original_value} "
                f"{parameter.original_unit}"
            )
            evidence = EvidenceRecordV2(
                revision_id=revision.id,
                evidence_class="user_measurement",
                source_authority="user",
                physical_validation_status="unvalidated",
                extraction_confidence=None,
                artifact_hash=artifact.object_hash,
                measurement_method=parameter.measurement_method,
                measurement_unit=parameter.original_unit,
                observed_at=parameter.observed_at,
                recorded_observation=recorded_observation,
                source_locator=f"parameters.{parameter.name}",
                excerpt=recorded_observation,
                limitations=[MANUAL_ENTRY_LIMITATION],
            )
            db.add(evidence)
            db.flush()
            evidence_ids.append(evidence.id)
            value_state = "known"
            value = parameter.value.model_dump(mode="json")
            unit = parameter.unit
            original_value = parameter.original_value
            original_unit = parameter.original_unit
            reason = None
        else:
            value_state = "unknown"
            value = {"kind": "unknown", "reason": parameter.unknown_reason}
            unit = None
            original_value = None
            original_unit = None
            reason = parameter.unknown_reason

        claim = CandidateClaimV2(
            revision_id=revision.id,
            slot_id=slot.id,
            slot=slot,
            value_state=value_state,
            value=value if value_state == "known" else None,
            unit=unit,
            original_value=original_value,
            original_unit=original_unit,
            reason=reason,
            validity_conditions=list(parameter.validity_conditions),
            provenance="manual_entry_v2",
            finalized=False,
            fingerprint=None,
        )
        db.add(claim)
        db.flush()
        for evidence_id in evidence_ids:
            db.add(
                ClaimEvidenceLinkV2(
                    revision_id=revision.id,
                    claim_id=claim.id,
                    evidence_id=evidence_id,
                )
            )
        db.flush()

        claim.fingerprint = _claim_fingerprint(
            revision_id=revision.id,
            slot_id=slot.id,
            value_state=value_state,
            value=value,
            provenance=claim.provenance,
            evidence_ids=evidence_ids,
            validity_conditions=list(parameter.validity_conditions),
            unit=unit,
            original_value=original_value,
            original_unit=original_unit,
        )
        claim.finalized = True
        db.flush()

        selection = ClaimSelectionV2(
            revision_id=revision.id,
            slot_id=slot.id,
            claim_id=claim.id,
        )
        db.add(selection)
        db.flush()
        claims.append(claim)
        selections.append(selection)

    claim_ids = sorted(claim.id for claim in claims)
    gates = [
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=request.capability_id,
            phase="publication",
            required_review_type="component_identity",
            state="satisfied",
            satisfying_references=[component.id],
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=request.capability_id,
            phase="publication",
            required_review_type="source_artifact",
            state="satisfied",
            satisfying_references=[artifact.object_hash],
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=request.capability_id,
            phase="publication",
            required_review_type="claim_selection",
            state="satisfied",
            satisfying_references=claim_ids,
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=request.capability_id,
            phase="publication",
            required_review_type="claim_review",
            state="pending",
            satisfying_references=[],
            reason="Every selected claim requires an effective accepted review.",
        ),
        _package_consumer_gate(db, revision.id, request.capability_id),
    ]
    db.add_all(gates)
    db.flush()

    job.status = "succeeded"
    job.revision_id = revision.id
    job.artifact_hash = artifact.object_hash
    db.flush()
    return ManualComponentDraftResult(
        job,
        revision,
        tuple(slots),
        tuple(claims),
        tuple(selections),
        tuple(gates),
    )


def create_manual_component_draft(
    db: Session, *, request: ManualComponentDraftRequestV2, idempotency_key: str
) -> ManualComponentDraftResult:
    """Create or replay one manually entered Program 01A v2 component draft."""

    if request.schema_version != SCHEMA_VERSION:
        raise ManualComponentDraftError(
            "unsupported_schema",
            "the requested manual component draft schema version is not supported",
        )
    if not isinstance(idempotency_key, str) or not _IDEMPOTENCY_PATTERN.fullmatch(
        idempotency_key
    ):
        raise ManualComponentDraftError(
            "invalid_idempotency_key",
            "manual component draft idempotency key must be 16-128 ASCII "
            "token characters",
        )
    canonical_request = canonicalize_value(
        request.model_dump(mode="json", by_alias=True)
    )

    with db.begin():
        existing = db.scalar(
            sa.select(ManualComponentDraftJobV2).where(
                ManualComponentDraftJobV2.idempotency_key == idempotency_key
            )
        )
        if existing is not None:
            return _existing_result(db, existing, canonical_request)
        return _create_graph(
            db,
            request=request,
            idempotency_key=idempotency_key,
            canonical_request=canonical_request,
        )


__all__ = [
    "MANUAL_ENTRY_LIMITATION",
    "MANUAL_ENTRY_RECORD_MEDIA_TYPE",
    "ManualComponentDraftError",
    "ManualComponentDraftRequestV2",
    "ManualComponentDraftResult",
    "create_manual_component_draft",
]
