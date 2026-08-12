"""Pure deterministic compiler from a reviewed v2 draft to package bytes."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import hashlib
from typing import Literal

from pydantic import ValidationError
import sqlalchemy as sa
from sqlalchemy.orm import Session

from .canonical_json import (
    MAX_RAW_BYTES,
    CanonicalJsonError,
    canonicalize_value,
    verify_canonical_bytes,
)
from .claim_identity_v2 import claim_evidence_ids, compute_claim_fingerprint
from .contracts_v2 import (
    SCHEMA_ID,
    SCHEMA_VERSION,
    ExecutionComponentV2,
)
from .models_v1 import Component, ComponentRevision, Manufacturer
from .models_v2 import (
    ArtifactObjectV2,
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
    EvidenceParentClaimV2,
    EvidenceParentEvidenceV2,
    EvidenceRecordV2,
    ParameterSlotV2,
)


MAX_PACKAGE_BYTES = MAX_RAW_BYTES
PACKAGE_COMPILER_VERSION = "0.2.0"


class PackageCompilationError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class CompiledExecutionComponentV2:
    value: dict[str, object]
    canonical_bytes: bytes
    object_hash: str
    execution_readiness: Literal["ready", "blocked"]


def _fail(code: str, message: str, cause: BaseException | None = None):
    error = PackageCompilationError(code, message)
    if cause is None:
        raise error
    raise error from cause


def _load_revision_identity(
    db: Session, revision_id: str
) -> tuple[ComponentRevision, Component, Manufacturer, str]:
    revision = db.get(ComponentRevision, revision_id)
    if revision is None:
        _fail("revision_not_found", f"revision {revision_id!r} does not exist")
    if (
        revision.status != "draft"
        or revision.publication_integrity != "v2_draft"
        or revision.contract_schema_id != SCHEMA_ID
        or revision.contract_schema_version != SCHEMA_VERSION
        or revision.draft_version is None
        or revision.draft_version < 0
    ):
        _fail(
            "revision_not_reviewed",
            "package compilation requires a reviewed unpublished v2 draft",
        )
    capabilities = revision.supported_recipes
    if (
        not isinstance(capabilities, list)
        or len(capabilities) != 1
        or not isinstance(capabilities[0], str)
        or not capabilities[0]
    ):
        _fail(
            "capability_identity_invalid",
            "v2 draft must select exactly one package capability",
        )
    component = db.get(Component, revision.component_id)
    if component is None:
        _fail("component_missing", "v2 revision references a missing component")
    manufacturer = db.get(Manufacturer, component.manufacturer_id)
    if manufacturer is None:
        _fail("manufacturer_missing", "component references a missing manufacturer")
    return revision, component, manufacturer, capabilities[0]


def _load_selected_claims(
    db: Session, revision_id: str
) -> tuple[list[ParameterSlotV2], list[CandidateClaimV2], dict[str, str]]:
    slots = list(
        db.scalars(
            sa.select(ParameterSlotV2)
            .where(ParameterSlotV2.revision_id == revision_id)
            .order_by(ParameterSlotV2.name, ParameterSlotV2.id)
        )
    )
    if not slots:
        _fail("no_parameter_slots", "reviewed draft contains no parameter slots")
    selections = list(
        db.scalars(
            sa.select(ClaimSelectionV2).where(
                ClaimSelectionV2.revision_id == revision_id
            )
        )
    )
    selection_by_slot = {selection.slot_id: selection.claim_id for selection in selections}
    if len(selection_by_slot) != len(slots) or any(
        slot.id not in selection_by_slot for slot in slots
    ):
        _fail(
            "missing_selection",
            "every package parameter slot requires exactly one current selection",
        )
    selected_ids = list(selection_by_slot.values())
    claims = list(
        db.scalars(
            sa.select(CandidateClaimV2).where(
                CandidateClaimV2.id.in_(selected_ids)
            )
        )
    )
    by_id = {claim.id: claim for claim in claims}
    ordered_claims: list[CandidateClaimV2] = []
    for claim_id in selected_ids:
        claim = by_id.get(claim_id)
        if claim is None:
            _fail("selected_claim_missing", f"selected claim {claim_id!r} is missing")
        if claim.revision_id != revision_id:
            _fail(
                "selected_claim_cross_revision",
                f"selected claim {claim_id!r} belongs to another revision",
            )
        if not claim.finalized or claim.fingerprint is None:
            _fail(
                "selected_claim_not_finalized",
                f"selected claim {claim_id!r} is not finalized",
            )
        try:
            fingerprint = compute_claim_fingerprint(db, claim)
        except ValueError as exc:
            _fail(
                "claim_integrity_failure",
                f"selected claim {claim_id!r} cannot be fingerprinted",
                exc,
            )
        if fingerprint != claim.fingerprint:
            _fail(
                "claim_fingerprint_mismatch",
                f"selected claim {claim_id!r} does not match its fingerprint",
            )
        ordered_claims.append(claim)
    ordered_claims.sort(key=lambda claim: claim.id)
    return slots, ordered_claims, selection_by_slot


def _load_effective_reviews(
    db: Session, revision_id: str, claim_ids: list[str]
) -> dict[str, ClaimReviewEventV2]:
    events = list(
        db.scalars(
            sa.select(ClaimReviewEventV2)
            .where(
                ClaimReviewEventV2.revision_id == revision_id,
                ClaimReviewEventV2.claim_id.in_(claim_ids),
            )
            .order_by(
                ClaimReviewEventV2.applied_draft_version,
                ClaimReviewEventV2.id,
            )
        )
    )
    effective: dict[str, ClaimReviewEventV2] = {}
    for event in events:
        effective[event.claim_id] = event
    return effective


def _load_evidence(
    db: Session, revision_id: str, claims: list[CandidateClaimV2]
) -> list[EvidenceRecordV2]:
    evidence_ids = sorted(
        {
            evidence_id
            for claim in claims
            for evidence_id in claim_evidence_ids(db, claim)
        }
    )
    if not evidence_ids:
        return []
    records = list(
        db.scalars(
            sa.select(EvidenceRecordV2)
            .where(EvidenceRecordV2.id.in_(evidence_ids))
            .order_by(EvidenceRecordV2.id)
        )
    )
    by_id = {record.id: record for record in records}
    for evidence_id in evidence_ids:
        record = by_id.get(evidence_id)
        if record is None:
            _fail(
                "evidence_missing",
                f"selected claim references missing evidence {evidence_id!r}",
            )
        if record.revision_id != revision_id:
            _fail(
                "evidence_cross_revision",
                f"evidence {evidence_id!r} belongs to another revision",
            )
    return [by_id[evidence_id] for evidence_id in evidence_ids]


def _load_gates(
    db: Session, revision_id: str, capability_id: str
) -> list[CapabilityGateV2]:
    gates = list(
        db.scalars(
            sa.select(CapabilityGateV2)
            .where(
                CapabilityGateV2.revision_id == revision_id,
                CapabilityGateV2.capability_id == capability_id,
            )
            .order_by(CapabilityGateV2.id)
        )
    )
    publication = [gate for gate in gates if gate.phase == "publication"]
    if not publication:
        _fail(
            "publication_gate_missing",
            "selected capability declares no publication gates",
        )
    if any(gate.state != "satisfied" for gate in publication):
        _fail(
            "publication_gate_unresolved",
            "selected capability has an unresolved publication gate",
        )
    return gates


def _artifact_hashes(
    evidence: list[EvidenceRecordV2], gates: list[CapabilityGateV2]
) -> list[str]:
    hashes = {
        record.artifact_hash
        for record in evidence
        if record.artifact_hash is not None
    }
    for gate in gates:
        if gate.required_review_type == "source_artifact":
            hashes.update(
                reference
                for reference in gate.satisfying_references
                if reference.startswith("sha256:")
            )
    return sorted(hashes)


def _load_verified_artifacts(
    db: Session, hashes: list[str]
) -> list[ArtifactObjectV2]:
    artifacts: list[ArtifactObjectV2] = []
    for identity in hashes:
        artifact = db.get(ArtifactObjectV2, identity)
        if artifact is None:
            _fail("artifact_missing", f"source artifact {identity!r} is missing")
        payload = bytes(artifact.payload_bytes)
        recomputed = f"sha256:{hashlib.sha256(payload).hexdigest()}"
        if artifact.byte_length != len(payload) or recomputed != identity:
            _fail(
                "artifact_integrity_failure",
                f"source artifact {identity!r} failed byte verification",
            )
        artifacts.append(artifact)
    return artifacts


def _claim_value(
    db: Session, claim: CandidateClaimV2
) -> dict[str, object]:
    value: dict[str, object] = {
        "claim_id": claim.id,
        "revision_id": claim.revision_id,
        "slot_id": claim.slot_id,
        "value_state": claim.value_state,
        "value": (
            claim.value
            if claim.value_state == "known"
            else {"kind": "unknown", "reason": claim.reason}
        ),
        "validity_conditions": list(claim.validity_conditions),
        "provenance": claim.provenance,
        "evidence_ids": claim_evidence_ids(db, claim),
        "claim_fingerprint": claim.fingerprint,
    }
    if claim.value_state == "known":
        value.update(
            {
                "unit": claim.unit,
                "original_value": claim.original_value,
                "original_unit": claim.original_unit,
            }
        )
    return value


def _evidence_value(db: Session, record: EvidenceRecordV2) -> dict[str, object]:
    value: dict[str, object] = {
        "evidence_id": record.id,
        "revision_id": record.revision_id,
        "evidence_class": record.evidence_class,
        "source_authority": record.source_authority,
        "physical_validation_status": record.physical_validation_status,
        "extraction_confidence": record.extraction_confidence,
        "source_uri": record.source_uri,
        "source_locator": record.source_locator,
        "excerpt": record.excerpt,
        "page": record.page,
        "limitations": list(record.limitations),
    }
    if record.evidence_class == "manufacturer_document":
        value.update(
            {
                "artifact_hash": record.artifact_hash,
                "document_identity": record.document_identity,
            }
        )
    elif record.evidence_class == "private_upload":
        value.update(
            {
                "artifact_hash": record.artifact_hash,
                "local_provenance": record.local_provenance,
            }
        )
    elif record.evidence_class == "user_measurement":
        value.update(
            {
                "measurement_method": record.measurement_method,
                "measurement_unit": record.measurement_unit,
                "observed_at": record.observed_at,
                "artifact_hash": record.artifact_hash,
                "recorded_observation": record.recorded_observation,
            }
        )
    elif record.evidence_class == "derived_claim":
        value.update(
            {
                "derivation_method": record.derivation_method,
                "parent_claim_ids": list(
                    db.scalars(
                        sa.select(EvidenceParentClaimV2.parent_claim_id)
                        .where(
                            EvidenceParentClaimV2.revision_id == record.revision_id,
                            EvidenceParentClaimV2.evidence_id == record.id,
                        )
                        .order_by(EvidenceParentClaimV2.parent_claim_id)
                    )
                ),
                "parent_evidence_ids": list(
                    db.scalars(
                        sa.select(EvidenceParentEvidenceV2.parent_evidence_id)
                        .where(
                            EvidenceParentEvidenceV2.revision_id == record.revision_id,
                            EvidenceParentEvidenceV2.evidence_id == record.id,
                        )
                        .order_by(EvidenceParentEvidenceV2.parent_evidence_id)
                    )
                ),
            }
        )
    elif record.evidence_class == "validation_observation":
        value.update(
            {
                "test_provenance": record.test_provenance,
                "observed_at": record.observed_at,
                "recorded_observation": record.recorded_observation,
                "artifact_hash": record.artifact_hash,
            }
        )
    return value


def _gate_value(gate: CapabilityGateV2) -> dict[str, object]:
    return {
        "gate_id": gate.id,
        "capability_id": gate.capability_id,
        "phase": gate.phase,
        "required_review_type": gate.required_review_type,
        "state": gate.state,
        "satisfying_reference_ids": sorted(gate.satisfying_references),
        "reason": gate.reason,
    }


def _build_execution_value(
    *,
    db: Session,
    revision: ComponentRevision,
    component: Component,
    manufacturer: Manufacturer,
    capability_id: str,
    slots: list[ParameterSlotV2],
    claims: list[CandidateClaimV2],
    selection_by_slot: dict[str, str],
    reviews: dict[str, ClaimReviewEventV2],
    evidence: list[EvidenceRecordV2],
    artifacts: list[ArtifactObjectV2],
    gates: list[CapabilityGateV2],
) -> dict[str, object]:
    execution_gates = [gate for gate in gates if gate.phase == "execution"]
    execution_readiness = (
        "ready"
        if all(gate.state == "satisfied" for gate in execution_gates)
        else "blocked"
    )
    return {
        "$schema": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "package_kind": "component_execution_input",
        "package_compiler": {
            "name": "prometheus_python",
            "version": PACKAGE_COMPILER_VERSION,
        },
        "revision_id": revision.id,
        "reviewed_draft_version": revision.draft_version,
        "component": {
            "component_id": component.id,
            "manufacturer": manufacturer.name,
            "part_number": component.part_number,
            "revision": revision.revision,
            "component_class": component.model_class,
        },
        "capability_id": capability_id,
        "artifacts": [
            {
                "artifact_hash": artifact.object_hash,
                "media_type": artifact.media_type,
                "byte_length": artifact.byte_length,
                "filename": artifact.original_filename,
                "artifact_role": "source_evidence",
            }
            for artifact in artifacts
        ],
        "parameter_slots": [
            {
                "slot_id": slot.id,
                "name": slot.name,
                "quantity": slot.quantity,
                "dimension": slot.dimension,
                "required_for_execution": slot.required_for_execution,
                "selected_claim_id": selection_by_slot[slot.id],
            }
            for slot in slots
        ],
        "claims": [_claim_value(db, claim) for claim in claims],
        "evidence": [_evidence_value(db, record) for record in evidence],
        "claim_reviews": [
            {
                "review_event_id": event.id,
                "revision_id": event.revision_id,
                "claim_id": event.claim_id,
                "reviewed_claim_fingerprint": event.claim_fingerprint,
                "decision": event.decision,
                "reviewed_by": event.reviewed_by,
                "note": event.note,
                "reviewed_at": event.reviewed_at,
                "applied_draft_version": event.applied_draft_version,
            }
            for event in sorted(reviews.values(), key=lambda item: item.id)
        ],
        "gates": [_gate_value(gate) for gate in gates],
        "execution_readiness": execution_readiness,
        "missing_information": sorted(
            list(revision.missing_information),
            key=lambda item: item["missing_information_id"],
        ),
        "limitations": sorted(
            list(revision.limitations),
            key=lambda item: item["limitation_id"],
        ),
        "authority": {
            "package_role": "reviewed_input",
            "engineering_decision_authority": "prometheus_cpp",
            "authority_role": "input_only",
        },
    }


def compile_execution_component(
    db: Session,
    revision_id: str,
    *,
    schema_id: str = SCHEMA_ID,
    schema_version: str = SCHEMA_VERSION,
    stage_callback: Callable[[str], None] | None = None,
) -> CompiledExecutionComponentV2:
    """Validate and compile a reviewed draft without mutating persistence."""

    if schema_id != SCHEMA_ID or schema_version != SCHEMA_VERSION:
        _fail(
            "unsupported_schema",
            "requested execution-component schema is not supported",
        )
    revision, component, manufacturer, capability_id = _load_revision_identity(
        db, revision_id
    )
    slots, claims, selection_by_slot = _load_selected_claims(db, revision_id)
    claim_ids = [claim.id for claim in claims]
    reviews = _load_effective_reviews(db, revision_id, claim_ids)
    for claim in claims:
        review = reviews.get(claim.id)
        if review is None:
            _fail(
                "effective_review_missing",
                f"selected claim {claim.id!r} has no effective review",
            )
        if review.decision != "accepted":
            _fail(
                "effective_review_not_accepted",
                f"selected claim {claim.id!r} is not effectively accepted",
            )
        if review.claim_fingerprint != claim.fingerprint:
            _fail(
                "review_fingerprint_mismatch",
                f"effective review for claim {claim.id!r} targets another fingerprint",
            )
        if review.applied_draft_version > revision.draft_version:
            _fail(
                "review_version_invalid",
                f"effective review for claim {claim.id!r} is newer than the draft",
            )
    evidence = _load_evidence(db, revision_id, claims)
    if any(record.revision_id != revision_id for record in evidence):
        _fail(
            "evidence_cross_revision",
            "selected claim evidence includes a different revision",
        )
    gates = _load_gates(db, revision_id, capability_id)
    try:
        artifacts = _load_verified_artifacts(db, _artifact_hashes(evidence, gates))
        raw_value = _build_execution_value(
            db=db,
            revision=revision,
            component=component,
            manufacturer=manufacturer,
            capability_id=capability_id,
            slots=slots,
            claims=claims,
            selection_by_slot=selection_by_slot,
            reviews=reviews,
            evidence=evidence,
            artifacts=artifacts,
            gates=gates,
        )
    except PackageCompilationError:
        raise
    except (KeyError, TypeError, ValueError) as exc:
        _fail(
            "contract_invalid",
            "stored draft graph cannot be represented by the v2 contract",
            exc,
        )
    if stage_callback is not None:
        stage_callback("after_package_compilation")
    try:
        contract = ExecutionComponentV2.model_validate(raw_value)
    except ValidationError as exc:
        _fail(
            "contract_invalid",
            "compiled draft does not satisfy the execution-component contract",
            exc,
        )
    value = contract.model_dump(mode="json", by_alias=True)
    if stage_callback is not None:
        stage_callback("after_schema_validation")
    try:
        canonical_bytes = canonicalize_value(value)
    except CanonicalJsonError as exc:
        _fail(
            "package_noncanonical",
            "validated package value cannot be canonicalized",
            exc,
        )
    if stage_callback is not None:
        stage_callback("after_canonicalization")
    if len(canonical_bytes) > MAX_PACKAGE_BYTES:
        _fail(
            "package_too_large",
            f"canonical package exceeds {MAX_PACKAGE_BYTES} bytes",
        )
    identity = f"sha256:{hashlib.sha256(canonical_bytes).hexdigest()}"
    if stage_callback is not None:
        stage_callback("after_hash_computation")
    try:
        verify_canonical_bytes(canonical_bytes)
    except CanonicalJsonError as exc:
        _fail(
            "package_noncanonical",
            "compiled bytes failed independent canonical verification",
            exc,
        )
    if stage_callback is not None:
        stage_callback("after_byte_verification")
    return CompiledExecutionComponentV2(
        value=value,
        canonical_bytes=canonical_bytes,
        object_hash=identity,
        execution_readiness=value["execution_readiness"],
    )


__all__ = [
    "CompiledExecutionComponentV2",
    "PackageCompilationError",
    "compile_execution_component",
]
