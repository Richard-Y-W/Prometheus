"""Exact offline fixture ingestion and v2 draft-graph construction."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
from typing import Annotated, Literal

from pydantic import Field, StrictStr, ValidationError, model_validator
import sqlalchemy as sa
from sqlalchemy.orm import Session

from .artifact_store import ingest_local_artifact
from .canonical_json import (
    CanonicalJsonError,
    canonicalize_value,
    parse_strict_json,
)
from .claim_identity_v2 import fingerprint_claim_fields
from .contracts_v2 import (
    SCHEMA_ID,
    SCHEMA_VERSION,
    ContractV2,
    EngineeringValueV2,
)
from .execution_contracts_v1 import PackageConsumerContractV1
from .fixture_catalog_v2 import (
    FIXTURE_IDS,
    PM_36_CAPABILITY,
    PM_36_FIXTURE_ID,
    FixtureDefinition,
    get_fixture_definition,
)
from .models import uid
from .models_v1 import Component, ComponentRevision, Manufacturer
from .models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimEvidenceLinkV2,
    ClaimSelectionV2,
    EvidenceRecordV2,
    FixtureIngestionJobV2,
    ParameterSlotV2,
)


_PM_36_DEFINITION = get_fixture_definition(PM_36_FIXTURE_ID)
FIXTURE_PATH = _PM_36_DEFINITION.source_path
FIXTURE_ID = _PM_36_DEFINITION.fixture_id
FIXTURE_ARTIFACT_HASH = _PM_36_DEFINITION.source_hash
CAPABILITY_ID = PM_36_CAPABILITY
_IDEMPOTENCY_PATTERN = re.compile(r"[A-Za-z0-9._:-]{16,128}\Z")
_Text = Annotated[StrictStr, Field(min_length=1, max_length=4096)]


def _normalize_identity(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


class FixtureDraftError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class _KnownFixtureParameter(ContractV2):
    name: _Text
    quantity: _Text
    dimension: _Text
    value: EngineeringValueV2
    unit: _Text
    original_value: _Text
    original_unit: _Text
    validity_conditions: list[_Text] = Field(max_length=1000)
    source_locator: _Text

    @model_validator(mode="after")
    def require_known_value_and_exact_locator(self):
        if self.value.kind == "unknown":
            raise ValueError("known fixture parameter cannot contain an unknown value")
        if self.source_locator != f"parameters.{self.name}":
            raise ValueError("fixture source locator must name its exact parameter")
        if len(self.validity_conditions) != len(set(self.validity_conditions)):
            raise ValueError("fixture validity conditions must be unique")
        return self


class _UnknownFixtureParameter(ContractV2):
    name: _Text
    quantity: _Text
    dimension: _Text
    unknown_reason: _Text
    validity_conditions: list[_Text] = Field(max_length=1000)
    source_locator: _Text

    @model_validator(mode="after")
    def require_exact_locator(self):
        if self.source_locator != f"parameters.{self.name}":
            raise ValueError("fixture source locator must name its exact parameter")
        if len(self.validity_conditions) != len(set(self.validity_conditions)):
            raise ValueError("fixture validity conditions must be unique")
        return self


_FixtureParameter = _KnownFixtureParameter | _UnknownFixtureParameter


class _FixturePayload(ContractV2):
    fixture_id: _Text
    schema_version: Literal["2.0.0"]
    manufacturer: _Text
    part_number: _Text
    revision: _Text
    component_class: _Text
    source_authority: Literal["synthetic_fixture"]
    physical_validation_status: Literal["unvalidated"]
    parameters: list[_FixtureParameter] = Field(min_length=1, max_length=1000)
    limitations: list[_Text] = Field(min_length=1, max_length=1000)

    @model_validator(mode="after")
    def require_unique_parameter_names_and_limitations(self):
        names = [parameter.name for parameter in self.parameters]
        if len(names) != len(set(names)):
            raise ValueError("fixture parameter names must be unique")
        if len(self.limitations) != len(set(self.limitations)):
            raise ValueError("fixture limitations must be unique")
        return self


@dataclass(frozen=True)
class FixtureDraftResult:
    ingestion_job: FixtureIngestionJobV2
    revision: ComponentRevision
    slots: tuple[ParameterSlotV2, ...]
    claims: tuple[CandidateClaimV2, ...]
    selections: tuple[ClaimSelectionV2, ...]
    gates: tuple[CapabilityGateV2, ...]


def _load_fixture(
    payload_bytes: bytes, definition: FixtureDefinition
) -> _FixturePayload:
    try:
        value = parse_strict_json(payload_bytes)
        fixture = _FixturePayload.model_validate(value)
        expected_identity = (
            definition.fixture_id,
            definition.manufacturer,
            definition.part_number,
            definition.revision,
            definition.component_class,
        )
        actual_identity = (
            fixture.fixture_id,
            fixture.manufacturer,
            fixture.part_number,
            fixture.revision,
            fixture.component_class,
        )
        if actual_identity != expected_identity:
            raise ValueError("fixture identity does not match its catalog entry")
        return fixture
    except (CanonicalJsonError, ValidationError, ValueError) as exc:
        raise FixtureDraftError(
            "fixture_contract_invalid",
            "the exact v2 fixture does not satisfy its closed local contract",
        ) from exc


def _load_consumer(payload_bytes: bytes) -> PackageConsumerContractV1:
    try:
        value = parse_strict_json(payload_bytes)
        consumer = PackageConsumerContractV1.model_validate(value)
        if canonicalize_value(
            consumer.model_dump(mode="json", by_alias=True)
        ) != payload_bytes:
            raise ValueError("consumer artifact must contain canonical bytes")
        return consumer
    except (CanonicalJsonError, ValidationError, ValueError) as exc:
        raise FixtureDraftError(
            "fixture_consumer_invalid",
            "the exact consumer artifact does not satisfy its closed contract",
        ) from exc


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
    job: FixtureIngestionJobV2,
    revision: ComponentRevision,
) -> FixtureDraftResult:
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
    return FixtureDraftResult(job, revision, slots, claims, selections, gates)


def _existing_result(
    db: Session, job: FixtureIngestionJobV2, fixture_id: str
) -> FixtureDraftResult:
    if job.fixture_id != fixture_id:
        raise FixtureDraftError(
            "fixture_idempotency_conflict",
            "the idempotency key is already bound to another fixture",
        )
    if job.status != "succeeded" or job.revision_id is None:
        raise FixtureDraftError(
            "fixture_ingestion_incomplete",
            "the fixture idempotency record is not a completed draft",
        )
    revision = db.get(ComponentRevision, job.revision_id)
    if revision is None:
        raise FixtureDraftError(
            "fixture_integrity_failure",
            "the completed fixture job references a missing revision",
        )
    return _ordered_graph(db, job, revision)


def _create_graph(
    db: Session, *, definition: FixtureDefinition, idempotency_key: str
) -> FixtureDraftResult:
    job = FixtureIngestionJobV2(
        fixture_id=definition.fixture_id,
        idempotency_key=idempotency_key,
        status="queued",
        revision_id=None,
        artifact_hash=None,
        error_code=None,
        error_message=None,
    )
    db.add(job)
    db.flush()

    artifact = ingest_local_artifact(
        db,
        source_path=definition.source_path,
        allowed_root=definition.source_path.parent,
        expected_hash=definition.source_hash,
        media_type="application/json",
    )
    fixture = _load_fixture(artifact.payload_bytes, definition)
    if definition.consumer_path is not None:
        if (
            definition.consumer_hash is None
            or definition.consumer_media_type is None
        ):
            raise FixtureDraftError(
                "fixture_catalog_invalid",
                "a consumer path requires exact hash and media type policy",
            )
        consumer_artifact = ingest_local_artifact(
            db,
            source_path=definition.consumer_path,
            allowed_root=definition.consumer_path.parent,
            expected_hash=definition.consumer_hash,
            media_type=definition.consumer_media_type,
        )
        _load_consumer(consumer_artifact.payload_bytes)
    job.status = "running"
    db.flush()

    manufacturer = db.scalar(
        sa.select(Manufacturer).where(Manufacturer.name == fixture.manufacturer)
    )
    if manufacturer is None:
        manufacturer = Manufacturer(
            name=fixture.manufacturer,
            normalized_name=_normalize_identity(fixture.manufacturer),
        )
        db.add(manufacturer)
        db.flush()
    elif manufacturer.normalized_name != _normalize_identity(fixture.manufacturer):
        raise FixtureDraftError(
            "fixture_identity_conflict",
            "stored manufacturer identity conflicts with the exact fixture",
        )

    component = db.scalar(
        sa.select(Component).where(
            Component.manufacturer_id == manufacturer.id,
            Component.part_number == fixture.part_number,
        )
    )
    if component is None:
        component = Component(
            manufacturer_id=manufacturer.id,
            part_number=fixture.part_number,
            normalized_part_number=_normalize_identity(fixture.part_number),
            family=definition.family,
            model_class=fixture.component_class,
        )
        db.add(component)
        db.flush()
    elif (
        component.normalized_part_number != _normalize_identity(fixture.part_number)
        or component.model_class != fixture.component_class
    ):
        raise FixtureDraftError(
            "fixture_identity_conflict",
            "stored component identity conflicts with the exact fixture",
        )

    if db.scalar(
        sa.select(ComponentRevision.id).where(
            ComponentRevision.component_id == component.id,
            ComponentRevision.revision == fixture.revision,
        )
    ) is not None:
        raise FixtureDraftError(
            "fixture_revision_exists",
            "the exact fixture revision already exists under another request",
        )

    unknown_parameters = [
        parameter
        for parameter in fixture.parameters
        if isinstance(parameter, _UnknownFixtureParameter)
    ]
    revision = ComponentRevision(
        component_id=component.id,
        revision=fixture.revision,
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
        supported_recipes=[definition.capability_id],
        missing_information=[],
        limitations=[],
    )
    db.add(revision)
    db.flush()

    ordered_parameters = sorted(fixture.parameters, key=lambda item: item.name)
    slots = [
        ParameterSlotV2(
            revision_id=revision.id,
            name=parameter.name,
            quantity=parameter.quantity,
            dimension=parameter.dimension,
            required_for_execution=(
                parameter.name in definition.required_for_execution
            ),
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
        for statement in (
            *fixture.limitations,
            definition.execution_limitation,
        )
    ]
    db.flush()

    claims: list[CandidateClaimV2] = []
    selections: list[ClaimSelectionV2] = []
    for parameter, slot in zip(ordered_parameters, slots, strict=True):
        evidence_ids: list[str] = []
        if isinstance(parameter, _KnownFixtureParameter):
            evidence = EvidenceRecordV2(
                revision_id=revision.id,
                evidence_class="private_upload",
                source_authority=fixture.source_authority,
                physical_validation_status=fixture.physical_validation_status,
                extraction_confidence=None,
                artifact_hash=artifact.object_hash,
                local_provenance=(
                    "Exact checked-in Prometheus synthetic fixture; not a "
                    "manufacturer publication."
                ),
                source_locator=parameter.source_locator,
                excerpt=(
                    f"{parameter.name}: {parameter.original_value} "
                    f"{parameter.original_unit}"
                ),
                limitations=list(fixture.limitations),
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
            provenance="fixture_json_v2",
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
            capability_id=definition.capability_id,
            phase="publication",
            required_review_type="component_identity",
            state="satisfied",
            satisfying_references=[component.id],
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=definition.capability_id,
            phase="publication",
            required_review_type="source_artifact",
            state="satisfied",
            satisfying_references=[artifact.object_hash],
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=definition.capability_id,
            phase="publication",
            required_review_type="claim_selection",
            state="satisfied",
            satisfying_references=claim_ids,
            reason=None,
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=definition.capability_id,
            phase="publication",
            required_review_type="claim_review",
            state="pending",
            satisfying_references=[],
            reason="Every selected claim requires an effective accepted review.",
        ),
        CapabilityGateV2(
            revision_id=revision.id,
            capability_id=definition.capability_id,
            phase="execution",
            required_review_type="package_consumer",
            state=definition.consumer_gate_state,
            satisfying_references=(
                [definition.consumer_hash]
                if definition.consumer_hash is not None
                else []
            ),
            reason=definition.consumer_gate_reason,
        ),
    ]
    db.add_all(gates)
    db.flush()

    job.status = "succeeded"
    job.revision_id = revision.id
    job.artifact_hash = artifact.object_hash
    db.flush()
    return FixtureDraftResult(
        job,
        revision,
        tuple(slots),
        tuple(claims),
        tuple(selections),
        tuple(gates),
    )


def create_fixture_draft(
    db: Session, *, fixture_id: str, idempotency_key: str
) -> FixtureDraftResult:
    """Create or replay the one exact Program 01A v2 fixture draft."""

    try:
        definition = get_fixture_definition(fixture_id)
    except (KeyError, TypeError):
        raise FixtureDraftError(
            "unsupported_fixture_id",
            f"fixture ID must be one of the exact ASCII catalog IDs {FIXTURE_IDS!r}",
        ) from None
    if not isinstance(idempotency_key, str) or not _IDEMPOTENCY_PATTERN.fullmatch(
        idempotency_key
    ):
        raise FixtureDraftError(
            "invalid_idempotency_key",
            "fixture idempotency key must be 16-128 ASCII token characters",
        )

    with db.begin():
        existing = db.scalar(
            sa.select(FixtureIngestionJobV2).where(
                FixtureIngestionJobV2.idempotency_key == idempotency_key
            )
        )
        if existing is not None:
            return _existing_result(db, existing, definition.fixture_id)
        return _create_graph(
            db, definition=definition, idempotency_key=idempotency_key
        )


__all__ = [
    "CAPABILITY_ID",
    "FIXTURE_ARTIFACT_HASH",
    "FIXTURE_ID",
    "FIXTURE_PATH",
    "FIXTURE_IDS",
    "FixtureDraftError",
    "FixtureDraftResult",
    "create_fixture_draft",
]
