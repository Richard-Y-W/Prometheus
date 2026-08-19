"""SQLAlchemy representation of the Program 01A v2 storage graph."""

from __future__ import annotations

from datetime import datetime

from sqlalchemy import (
    JSON,
    Boolean,
    CheckConstraint,
    Float,
    ForeignKey,
    ForeignKeyConstraint,
    Integer,
    String,
    Text,
    UniqueConstraint,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from .contracts_v2 import SCHEMA_ID, SCHEMA_VERSION
from .database import Base
from .db_types import ImmutableBytes, UtcTimestamp, utc_now
from .models import uid


HASH_CHECK = "length({column}) = 71 AND {column} LIKE 'sha256:%' AND lower({column}) = {column}"


class ArtifactObjectV2(Base):
    __tablename__ = "artifact_objects_v2"

    object_hash: Mapped[str] = mapped_column(String(71), primary_key=True)
    payload_bytes: Mapped[bytes] = mapped_column(ImmutableBytes())
    byte_length: Mapped[int] = mapped_column(Integer)
    media_type: Mapped[str] = mapped_column(String)
    origin_path: Mapped[str] = mapped_column(Text)
    original_filename: Mapped[str] = mapped_column(String)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    __table_args__ = (
        CheckConstraint(HASH_CHECK.format(column="object_hash"), name="ck_artifact_hash"),
        CheckConstraint("byte_length >= 0", name="ck_artifact_byte_length"),
    )


class PublishedObject(Base):
    __tablename__ = "published_objects"

    object_hash: Mapped[str] = mapped_column(String(71), primary_key=True)
    payload_bytes: Mapped[bytes] = mapped_column(ImmutableBytes())
    byte_length: Mapped[int] = mapped_column(Integer)
    media_type: Mapped[str] = mapped_column(String)
    schema_id: Mapped[str] = mapped_column(String, default=SCHEMA_ID)
    schema_version: Mapped[str] = mapped_column(String, default=SCHEMA_VERSION)
    canonicalization: Mapped[str] = mapped_column(String, default="RFC8785")
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    __table_args__ = (
        CheckConstraint(
            HASH_CHECK.format(column="object_hash"), name="ck_published_object_hash"
        ),
        CheckConstraint("byte_length >= 0", name="ck_published_byte_length"),
        CheckConstraint(
            f"schema_id = '{SCHEMA_ID}' AND schema_version = '{SCHEMA_VERSION}'",
            name="ck_published_schema",
        ),
        CheckConstraint(
            "canonicalization = 'RFC8785'", name="ck_published_canonicalization"
        ),
    )


class V2MutableRecord(Base):
    __abstract__ = True

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=uid)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    updated_at: Mapped[datetime] = mapped_column(
        UtcTimestamp(), default=utc_now, onupdate=utc_now
    )


class V2ImmutableRecord(Base):
    __abstract__ = True

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=uid)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)


class ParameterSlotV2(V2MutableRecord):
    __tablename__ = "parameter_slots_v2"

    revision_id: Mapped[str] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=False
    )
    name: Mapped[str] = mapped_column(String)
    quantity: Mapped[str] = mapped_column(String)
    dimension: Mapped[str] = mapped_column(String)
    required_for_execution: Mapped[bool] = mapped_column(Boolean)
    claims: Mapped[list["CandidateClaimV2"]] = relationship(
        back_populates="slot", order_by="CandidateClaimV2.id"
    )
    __table_args__ = (
        UniqueConstraint("revision_id", "id", name="uq_slot_revision_id"),
        UniqueConstraint("revision_id", "name", name="uq_slot_revision_name"),
    )


class CandidateClaimV2(V2MutableRecord):
    __tablename__ = "candidate_claims_v2"

    revision_id: Mapped[str] = mapped_column(String, nullable=False)
    slot_id: Mapped[str] = mapped_column(String(36), nullable=False)
    value_state: Mapped[str] = mapped_column(String)
    value: Mapped[dict | None] = mapped_column(
        "value_json", JSON(none_as_null=True), nullable=True
    )
    unit: Mapped[str | None] = mapped_column(String, nullable=True)
    original_value: Mapped[str | None] = mapped_column(Text, nullable=True)
    original_unit: Mapped[str | None] = mapped_column(String, nullable=True)
    reason: Mapped[str | None] = mapped_column(Text, nullable=True)
    validity_conditions: Mapped[list] = mapped_column(
        "validity_conditions_json", JSON, default=list
    )
    provenance: Mapped[str] = mapped_column(Text)
    finalized: Mapped[bool] = mapped_column(Boolean, default=False)
    fingerprint: Mapped[str | None] = mapped_column(String(71), nullable=True)
    slot: Mapped[ParameterSlotV2] = relationship(back_populates="claims")
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "slot_id"],
            ["parameter_slots_v2.revision_id", "parameter_slots_v2.id"],
            name="fk_claim_same_revision_slot",
        ),
        UniqueConstraint("revision_id", "id", name="uq_claim_revision_id"),
        UniqueConstraint(
            "revision_id", "slot_id", "id", name="uq_claim_revision_slot_id"
        ),
        CheckConstraint(
            "(value_state = 'known' AND value_json IS NOT NULL AND unit IS NOT NULL "
            "AND original_value IS NOT NULL AND original_unit IS NOT NULL AND "
            "reason IS NULL) OR (value_state = 'unknown' AND value_json IS NULL "
            "AND unit IS NULL AND original_value IS NULL AND original_unit IS NULL "
            "AND reason IS NOT NULL)",
            name="ck_claim_known_unknown_shape",
        ),
        CheckConstraint(
            "(finalized IS FALSE AND fingerprint IS NULL) OR "
            "(finalized IS TRUE AND fingerprint IS NOT NULL)",
            name="ck_claim_finalization_shape",
        ),
        CheckConstraint(
            "fingerprint IS NULL OR " + HASH_CHECK.format(column="fingerprint"),
            name="ck_claim_fingerprint",
        ),
    )


class EvidenceRecordV2(V2ImmutableRecord):
    __tablename__ = "evidence_records_v2"

    revision_id: Mapped[str] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=False
    )
    evidence_class: Mapped[str] = mapped_column(String)
    source_authority: Mapped[str] = mapped_column(String)
    physical_validation_status: Mapped[str] = mapped_column(String)
    extraction_confidence: Mapped[float | None] = mapped_column(Float, nullable=True)
    artifact_hash: Mapped[str | None] = mapped_column(
        ForeignKey("artifact_objects_v2.object_hash"), nullable=True
    )
    local_provenance: Mapped[str | None] = mapped_column(Text, nullable=True)
    document_identity: Mapped[str | None] = mapped_column(String, nullable=True)
    source_uri: Mapped[str | None] = mapped_column(Text, nullable=True)
    source_locator: Mapped[str | None] = mapped_column(Text, nullable=True)
    excerpt: Mapped[str | None] = mapped_column(Text, nullable=True)
    page: Mapped[int | None] = mapped_column(Integer, nullable=True)
    measurement_method: Mapped[str | None] = mapped_column(Text, nullable=True)
    measurement_unit: Mapped[str | None] = mapped_column(String, nullable=True)
    observed_at: Mapped[datetime | None] = mapped_column(
        UtcTimestamp(), nullable=True
    )
    recorded_observation: Mapped[str | None] = mapped_column(Text, nullable=True)
    derivation_method: Mapped[str | None] = mapped_column(Text, nullable=True)
    test_provenance: Mapped[str | None] = mapped_column(Text, nullable=True)
    limitations: Mapped[list] = mapped_column("limitations_json", JSON, default=list)
    __table_args__ = (
        UniqueConstraint("revision_id", "id", name="uq_evidence_revision_id"),
        CheckConstraint(
            "evidence_class IN ('manufacturer_document','private_upload',"
            "'user_measurement','derived_claim','validation_observation')",
            name="ck_evidence_class",
        ),
        CheckConstraint(
            "source_authority IN ('manufacturer','supplier','private_provider',"
            "'user','prometheus_derivation','validation_activity',"
            "'synthetic_fixture','unknown')",
            name="ck_evidence_source_authority",
        ),
        CheckConstraint(
            "physical_validation_status IN ('unvalidated','component_validated',"
            "'system_validated')",
            name="ck_evidence_physical_status",
        ),
        CheckConstraint(
            "extraction_confidence IS NULL OR "
            "(extraction_confidence >= 0 AND extraction_confidence <= 1)",
            name="ck_evidence_extraction_confidence",
        ),
        CheckConstraint(
            "(source_locator IS NULL AND excerpt IS NULL AND page IS NULL) OR "
            "(source_locator IS NOT NULL AND excerpt IS NOT NULL)",
            name="ck_evidence_locator_excerpt",
        ),
        CheckConstraint(
            "(evidence_class = 'manufacturer_document' AND artifact_hash IS NOT NULL "
            "AND document_identity IS NOT NULL) OR "
            "(evidence_class = 'private_upload' AND artifact_hash IS NOT NULL "
            "AND local_provenance IS NOT NULL) OR "
            "(evidence_class = 'user_measurement' AND measurement_method IS NOT NULL "
            "AND measurement_unit IS NOT NULL AND observed_at IS NOT NULL AND "
            "(artifact_hash IS NOT NULL OR recorded_observation IS NOT NULL)) OR "
            "(evidence_class = 'derived_claim' AND derivation_method IS NOT NULL) OR "
            "(evidence_class = 'validation_observation' AND test_provenance IS NOT NULL "
            "AND observed_at IS NOT NULL AND recorded_observation IS NOT NULL)",
            name="ck_evidence_class_shape",
        ),
    )


class ClaimEvidenceLinkV2(Base):
    __tablename__ = "claim_evidence_links_v2"

    revision_id: Mapped[str] = mapped_column(String, primary_key=True)
    claim_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    evidence_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_claim_link_same_revision_claim",
        ),
        ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_claim_link_same_revision_evidence",
        ),
    )


class EvidenceParentClaimV2(Base):
    __tablename__ = "evidence_parent_claims_v2"

    revision_id: Mapped[str] = mapped_column(String, primary_key=True)
    evidence_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    parent_claim_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_same_revision_evidence",
        ),
        ForeignKeyConstraint(
            ["revision_id", "parent_claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_evidence_parent_same_revision_claim",
        ),
    )


class EvidenceParentEvidenceV2(Base):
    __tablename__ = "evidence_parent_evidence_v2"

    revision_id: Mapped[str] = mapped_column(String, primary_key=True)
    evidence_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    parent_evidence_id: Mapped[str] = mapped_column(String(36), primary_key=True)
    created_at: Mapped[datetime] = mapped_column(UtcTimestamp(), default=utc_now)
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_evidence_child",
        ),
        ForeignKeyConstraint(
            ["revision_id", "parent_evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_evidence_parent",
        ),
        CheckConstraint(
            "evidence_id <> parent_evidence_id", name="ck_evidence_not_own_parent"
        ),
    )


class ClaimSelectionV2(V2MutableRecord):
    __tablename__ = "claim_selections_v2"

    revision_id: Mapped[str] = mapped_column(String, nullable=False)
    slot_id: Mapped[str] = mapped_column(String(36), nullable=False)
    claim_id: Mapped[str] = mapped_column(String(36), nullable=False)
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "slot_id", "claim_id"],
            [
                "candidate_claims_v2.revision_id",
                "candidate_claims_v2.slot_id",
                "candidate_claims_v2.id",
            ],
            name="fk_selection_same_revision_slot_claim",
        ),
        UniqueConstraint(
            "revision_id", "slot_id", name="uq_selection_revision_slot"
        ),
    )


class ClaimReviewEventV2(V2ImmutableRecord):
    __tablename__ = "claim_review_events_v2"

    revision_id: Mapped[str] = mapped_column(String, nullable=False)
    claim_id: Mapped[str] = mapped_column(String(36), nullable=False)
    claim_fingerprint: Mapped[str] = mapped_column(String(71))
    decision: Mapped[str] = mapped_column(String)
    reviewed_by: Mapped[str] = mapped_column(String)
    note: Mapped[str] = mapped_column(Text)
    reviewed_at: Mapped[datetime] = mapped_column(UtcTimestamp())
    applied_draft_version: Mapped[int] = mapped_column(Integer)
    __table_args__ = (
        ForeignKeyConstraint(
            ["revision_id", "claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_review_same_revision_claim",
        ),
        UniqueConstraint("revision_id", "id", name="uq_review_revision_id"),
        UniqueConstraint(
            "claim_id", "applied_draft_version", name="uq_review_claim_version"
        ),
        CheckConstraint(
            "decision IN ('accepted','rejected','ambiguous')",
            name="ck_review_decision",
        ),
        CheckConstraint("applied_draft_version >= 1", name="ck_review_version"),
        CheckConstraint(
            HASH_CHECK.format(column="claim_fingerprint"),
            name="ck_review_fingerprint",
        ),
    )


class CapabilityGateV2(V2MutableRecord):
    __tablename__ = "capability_gates_v2"

    revision_id: Mapped[str] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=False
    )
    capability_id: Mapped[str] = mapped_column(String)
    phase: Mapped[str] = mapped_column(String)
    required_review_type: Mapped[str] = mapped_column(String)
    state: Mapped[str] = mapped_column(String)
    satisfying_references: Mapped[list] = mapped_column(
        "satisfying_references_json", JSON, default=list
    )
    reason: Mapped[str | None] = mapped_column(Text, nullable=True)
    __table_args__ = (
        UniqueConstraint("revision_id", "id", name="uq_gate_revision_id"),
        CheckConstraint("phase IN ('publication','execution')", name="ck_gate_phase"),
        CheckConstraint(
            "required_review_type IN ('component_identity','source_artifact',"
            "'claim_selection','claim_review','package_consumer')",
            name="ck_gate_review_type",
        ),
        CheckConstraint(
            "state IN ('pending','satisfied','blocked')", name="ck_gate_state"
        ),
    )


class FixtureIngestionJobV2(V2MutableRecord):
    __tablename__ = "fixture_ingestion_jobs_v2"

    fixture_id: Mapped[str] = mapped_column(String)
    idempotency_key: Mapped[str] = mapped_column(String, unique=True)
    status: Mapped[str] = mapped_column(String)
    revision_id: Mapped[str | None] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=True
    )
    artifact_hash: Mapped[str | None] = mapped_column(
        ForeignKey("artifact_objects_v2.object_hash"), nullable=True
    )
    error_code: Mapped[str | None] = mapped_column(String, nullable=True)
    error_message: Mapped[str | None] = mapped_column(Text, nullable=True)
    __table_args__ = (
        CheckConstraint(
            "status IN ('queued','running','succeeded','failed','cancelled','timed_out')",
            name="ck_ingestion_status",
        ),
        CheckConstraint(
            "(status IN ('queued','running') AND revision_id IS NULL AND "
            "artifact_hash IS NULL "
            "AND error_code IS NULL AND error_message IS NULL) OR "
            "(status = 'succeeded' AND revision_id IS NOT NULL AND "
            "artifact_hash IS NOT NULL AND error_code IS NULL AND "
            "error_message IS NULL) OR (status IN "
            "('failed','cancelled','timed_out') AND revision_id IS NULL AND "
            "artifact_hash IS NULL AND error_code IS NOT NULL AND "
            "error_message IS NOT NULL)",
            name="ck_ingestion_state_shape",
        ),
    )


class ManualComponentDraftJobV2(V2MutableRecord):
    __tablename__ = "manual_component_draft_jobs_v2"

    idempotency_key: Mapped[str] = mapped_column(String, unique=True)
    request_fingerprint: Mapped[str] = mapped_column(String(71))
    status: Mapped[str] = mapped_column(String)
    revision_id: Mapped[str | None] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=True
    )
    artifact_hash: Mapped[str | None] = mapped_column(
        ForeignKey("artifact_objects_v2.object_hash"), nullable=True
    )
    error_code: Mapped[str | None] = mapped_column(String, nullable=True)
    error_message: Mapped[str | None] = mapped_column(Text, nullable=True)
    __table_args__ = (
        CheckConstraint(
            HASH_CHECK.format(column="request_fingerprint"),
            name="ck_manual_draft_fingerprint",
        ),
        CheckConstraint(
            "status IN ('queued','running','succeeded','failed','cancelled','timed_out')",
            name="ck_manual_draft_status",
        ),
        CheckConstraint(
            "(status IN ('queued','running') AND revision_id IS NULL AND "
            "artifact_hash IS NULL "
            "AND error_code IS NULL AND error_message IS NULL) OR "
            "(status = 'succeeded' AND revision_id IS NOT NULL AND "
            "artifact_hash IS NOT NULL AND error_code IS NULL AND "
            "error_message IS NULL) OR (status IN "
            "('failed','cancelled','timed_out') AND revision_id IS NULL AND "
            "artifact_hash IS NULL AND error_code IS NOT NULL AND "
            "error_message IS NOT NULL)",
            name="ck_manual_draft_state_shape",
        ),
    )


class ComponentAcquisitionJobV2(V2MutableRecord):
    __tablename__ = "component_acquisition_jobs_v2"

    idempotency_key: Mapped[str] = mapped_column(String, unique=True)
    request_fingerprint: Mapped[str] = mapped_column(String(71))
    url: Mapped[str] = mapped_column(Text)
    status: Mapped[str] = mapped_column(String)
    source_artifact_hash: Mapped[str | None] = mapped_column(
        ForeignKey("artifact_objects_v2.object_hash"), nullable=True
    )
    extracted_manufacturer: Mapped[str | None] = mapped_column(String, nullable=True)
    extracted_part_number: Mapped[str | None] = mapped_column(String, nullable=True)
    extraction_method: Mapped[str | None] = mapped_column(String, nullable=True)
    error_code: Mapped[str | None] = mapped_column(String, nullable=True)
    error_message: Mapped[str | None] = mapped_column(Text, nullable=True)
    __table_args__ = (
        CheckConstraint(
            HASH_CHECK.format(column="request_fingerprint"),
            name="ck_acquisition_fingerprint",
        ),
        CheckConstraint(
            "status IN ('queued','running','succeeded','failed','cancelled','timed_out')",
            name="ck_acquisition_status",
        ),
        CheckConstraint(
            "(status IN ('queued','running') AND source_artifact_hash IS NULL AND "
            "extracted_manufacturer IS NULL AND extracted_part_number IS NULL AND "
            "extraction_method IS NULL AND error_code IS NULL AND "
            "error_message IS NULL) OR "
            "(status = 'succeeded' AND source_artifact_hash IS NOT NULL AND "
            "extraction_method IS NOT NULL AND error_code IS NULL AND "
            "error_message IS NULL) OR (status IN "
            "('failed','cancelled','timed_out') AND source_artifact_hash IS NULL AND "
            "extracted_manufacturer IS NULL AND extracted_part_number IS NULL AND "
            "extraction_method IS NULL AND error_code IS NOT NULL AND "
            "error_message IS NOT NULL)",
            name="ck_acquisition_state_shape",
        ),
    )


class PublicationRequestV2(V2MutableRecord):
    __tablename__ = "publication_requests"

    operation: Mapped[str] = mapped_column(String)
    idempotency_key: Mapped[str] = mapped_column(String)
    revision_id: Mapped[str] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=False
    )
    request_fingerprint: Mapped[str] = mapped_column(String(71))
    state: Mapped[str] = mapped_column(String)
    response_status: Mapped[int | None] = mapped_column(Integer, nullable=True)
    response_body: Mapped[bytes | None] = mapped_column(
        ImmutableBytes(), nullable=True
    )
    response_headers: Mapped[dict | None] = mapped_column(
        "response_headers_json", JSON(none_as_null=True), nullable=True
    )
    published_object_hash: Mapped[str | None] = mapped_column(
        ForeignKey("published_objects.object_hash"), nullable=True
    )
    __table_args__ = (
        UniqueConstraint(
            "operation", "idempotency_key", name="uq_publication_operation_key"
        ),
        CheckConstraint("operation = 'publication'", name="ck_publication_operation"),
        CheckConstraint(
            "state IN ('in_progress','succeeded','terminal_failure')",
            name="ck_publication_state",
        ),
        CheckConstraint(
            HASH_CHECK.format(column="request_fingerprint"),
            name="ck_publication_fingerprint",
        ),
        CheckConstraint(
            "(state = 'in_progress' AND response_status IS NULL AND response_body "
            "IS NULL AND response_headers_json IS NULL AND published_object_hash "
            "IS NULL) OR (state = 'succeeded' AND response_status IS NOT NULL AND "
            "response_body IS NOT NULL AND response_headers_json IS NOT NULL AND "
            "published_object_hash IS NOT NULL) OR (state = 'terminal_failure' AND "
            "response_status IS NOT NULL AND response_body IS NOT NULL AND "
            "response_headers_json IS NOT NULL AND published_object_hash IS NULL)",
            name="ck_publication_state_shape",
        ),
    )


__all__ = [
    "ArtifactObjectV2",
    "CandidateClaimV2",
    "CapabilityGateV2",
    "ClaimEvidenceLinkV2",
    "ClaimReviewEventV2",
    "ClaimSelectionV2",
    "EvidenceParentClaimV2",
    "EvidenceParentEvidenceV2",
    "EvidenceRecordV2",
    "FixtureIngestionJobV2",
    "ManualComponentDraftJobV2",
    "ParameterSlotV2",
    "PublicationRequestV2",
    "PublishedObject",
]
