"""amended Program 01A trust boundary v2

Revision ID: a41f0c93e2d7
Revises: 7b6d91e2a4f0
Create Date: 2026-08-11
"""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op


revision: str = "a41f0c93e2d7"
down_revision: Union[str, None] = "7b6d91e2a4f0"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None

SCHEMA_ID = "urn:prometheus:schema:execution-component:2.0.0"
SCHEMA_VERSION = "2.0.0"
HASH_CHECK = (
    "length({column}) = 71 AND {column} LIKE 'sha256:%' "
    "AND lower({column}) = {column}"
)


def _is_sqlite() -> bool:
    return op.get_bind().dialect.name == "sqlite"


def _utc_type():
    if op.get_bind().dialect.name == "postgresql":
        return sa.DateTime(timezone=True)
    return sa.String(32)


def _hash_check(column: str) -> str:
    base = HASH_CHECK.format(column=column)
    if _is_sqlite():
        return base + f" AND substr({column}, 8) NOT GLOB '*[^0-9a-f]*'"
    return base + f" AND substring({column} from 8) ~ '^[0-9a-f]{{64}}$'"


def _uuid_check(column: str) -> str:
    base = (
        f"length({column}) = 36 AND lower({column}) = {column} AND "
        f"substr({column}, 9, 1) = '-' AND substr({column}, 14, 1) = '-' AND "
        f"substr({column}, 15, 1) = '4' AND substr({column}, 19, 1) = '-' AND "
        f"substr({column}, 20, 1) IN ('8','9','a','b') AND "
        f"substr({column}, 24, 1) = '-'"
    )
    if _is_sqlite():
        return base + f" AND replace({column}, '-', '') NOT GLOB '*[^0-9a-f]*'"
    return (
        base
        + f" AND {column} ~ '^[0-9a-f]{{8}}-[0-9a-f]{{4}}-4[0-9a-f]{{3}}-"
        "[89ab][0-9a-f]{3}-[0-9a-f]{12}$'"
    )


def _utc_checks(*columns: str) -> list[sa.CheckConstraint]:
    if not _is_sqlite():
        return []
    return [
        sa.CheckConstraint(
            f"{column} IS NULL OR (substr({column}, -1, 1) = 'Z' "
            f"AND datetime({column}) IS NOT NULL)",
            name=f"ck_{column}_utc",
        )
        for column in columns
    ]


def _canonical_utc(value: object) -> str:
    if not isinstance(value, str):
        raise RuntimeError(f"cannot migrate non-text publication timestamp {value!r}")
    try:
        parsed = datetime.fromisoformat(
            value[:-1] + "+00:00" if value.endswith("Z") else value
        )
    except ValueError as exc:
        raise RuntimeError(f"cannot migrate invalid publication timestamp {value!r}") from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise RuntimeError(f"cannot migrate naive publication timestamp {value!r}")
    return (
        parsed.astimezone(timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


def _normalize_legacy_publication_timestamps() -> None:
    connection = op.get_bind()
    rows = connection.execute(
        sa.text(
            "SELECT id, published_at FROM component_revisions "
            "WHERE published_at IS NOT NULL"
        )
    ).mappings()
    for row in rows:
        connection.execute(
            sa.text(
                "UPDATE component_revisions SET published_at=:published_at "
                "WHERE id=:id"
            ),
            {"published_at": _canonical_utc(row["published_at"]), "id": row["id"]},
        )


def _create_object_tables() -> None:
    op.create_table(
        "artifact_objects_v2",
        sa.Column("object_hash", sa.String(71), primary_key=True),
        sa.Column("payload_bytes", sa.LargeBinary(), nullable=False),
        sa.Column("byte_length", sa.Integer(), nullable=False),
        sa.Column("media_type", sa.String(), nullable=False),
        sa.Column("origin_path", sa.Text(), nullable=False),
        sa.Column("original_filename", sa.String(), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.CheckConstraint(
            _hash_check("object_hash"), name="ck_artifact_hash"
        ),
        sa.CheckConstraint("byte_length >= 0", name="ck_artifact_byte_length"),
        *_utc_checks("created_at"),
    )
    op.create_table(
        "published_objects",
        sa.Column("object_hash", sa.String(71), primary_key=True),
        sa.Column("payload_bytes", sa.LargeBinary(), nullable=False),
        sa.Column("byte_length", sa.Integer(), nullable=False),
        sa.Column("media_type", sa.String(), nullable=False),
        sa.Column("schema_id", sa.String(), nullable=False),
        sa.Column("schema_version", sa.String(), nullable=False),
        sa.Column("canonicalization", sa.String(), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.CheckConstraint(
            _hash_check("object_hash"),
            name="ck_published_object_hash",
        ),
        sa.CheckConstraint("byte_length >= 0", name="ck_published_byte_length"),
        sa.CheckConstraint(
            f"schema_id = '{SCHEMA_ID}' AND schema_version = '{SCHEMA_VERSION}'",
            name="ck_published_schema",
        ),
        sa.CheckConstraint(
            "canonicalization = 'RFC8785'", name="ck_published_canonicalization"
        ),
        *_utc_checks("created_at"),
    )


def _extend_component_revisions() -> None:
    op.add_column(
        "component_revisions", sa.Column("draft_version", sa.Integer(), nullable=True)
    )
    op.add_column(
        "component_revisions",
        sa.Column("contract_schema_id", sa.String(), nullable=True),
    )
    op.add_column(
        "component_revisions",
        sa.Column("contract_schema_version", sa.String(), nullable=True),
    )
    op.add_column(
        "component_revisions",
        sa.Column("publication_integrity", sa.String(), nullable=True),
    )
    op.add_column(
        "component_revisions",
        sa.Column("published_object_hash", sa.String(71), nullable=True),
    )
    if not _is_sqlite():
        op.create_foreign_key(
            "fk_revision_published_object",
            "component_revisions",
            "published_objects",
            ["published_object_hash"],
            ["object_hash"],
        )
    if op.get_bind().dialect.name == "postgresql":
        op.alter_column(
            "component_revisions",
            "published_at",
            existing_type=sa.String(),
            type_=sa.DateTime(timezone=True),
            postgresql_using="published_at::timestamptz",
            existing_nullable=True,
        )
    op.get_bind().execute(
        sa.text(
            "UPDATE component_revisions SET publication_integrity="
            "CASE WHEN status='published' THEN 'legacy_unsealed' ELSE NULL END"
        )
    )


def _create_claim_graph_tables() -> None:
    op.create_table(
        "parameter_slots_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=False,
        ),
        sa.Column("name", sa.String(), nullable=False),
        sa.Column("quantity", sa.String(), nullable=False),
        sa.Column("dimension", sa.String(), nullable=False),
        sa.Column("required_for_execution", sa.Boolean(), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.UniqueConstraint("revision_id", "id", name="uq_slot_revision_id"),
        sa.UniqueConstraint("revision_id", "name", name="uq_slot_revision_name"),
        sa.CheckConstraint(_uuid_check("id"), name="ck_slot_uuid4"),
        *_utc_checks("created_at", "updated_at"),
    )
    op.create_table(
        "candidate_claims_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("revision_id", sa.String(), nullable=False),
        sa.Column("slot_id", sa.String(36), nullable=False),
        sa.Column("value_state", sa.String(), nullable=False),
        sa.Column("value_json", sa.JSON(), nullable=True),
        sa.Column("unit", sa.String(), nullable=True),
        sa.Column("original_value", sa.Text(), nullable=True),
        sa.Column("original_unit", sa.String(), nullable=True),
        sa.Column("reason", sa.Text(), nullable=True),
        sa.Column("validity_conditions_json", sa.JSON(), nullable=False),
        sa.Column("provenance", sa.Text(), nullable=False),
        sa.Column("finalized", sa.Boolean(), nullable=False),
        sa.Column("fingerprint", sa.String(71), nullable=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "slot_id"],
            ["parameter_slots_v2.revision_id", "parameter_slots_v2.id"],
            name="fk_claim_same_revision_slot",
        ),
        sa.UniqueConstraint("revision_id", "id", name="uq_claim_revision_id"),
        sa.UniqueConstraint(
            "revision_id", "slot_id", "id", name="uq_claim_revision_slot_id"
        ),
        sa.CheckConstraint(_uuid_check("id"), name="ck_claim_uuid4"),
        sa.CheckConstraint(
            "(value_state = 'known' AND value_json IS NOT NULL AND unit IS NOT NULL "
            "AND original_value IS NOT NULL AND original_unit IS NOT NULL AND "
            "reason IS NULL) OR (value_state = 'unknown' AND value_json IS NULL "
            "AND unit IS NULL AND original_value IS NULL AND original_unit IS NULL "
            "AND reason IS NOT NULL)",
            name="ck_claim_known_unknown_shape",
        ),
        sa.CheckConstraint(
            "(finalized IS FALSE AND fingerprint IS NULL) OR "
            "(finalized IS TRUE AND fingerprint IS NOT NULL)",
            name="ck_claim_finalization_shape",
        ),
        sa.CheckConstraint(
            "fingerprint IS NULL OR " + _hash_check("fingerprint"),
            name="ck_claim_fingerprint",
        ),
        *_utc_checks("created_at", "updated_at"),
    )
    op.create_table(
        "evidence_records_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=False,
        ),
        sa.Column("evidence_class", sa.String(), nullable=False),
        sa.Column("source_authority", sa.String(), nullable=False),
        sa.Column("physical_validation_status", sa.String(), nullable=False),
        sa.Column("extraction_confidence", sa.Float(), nullable=True),
        sa.Column(
            "artifact_hash",
            sa.String(71),
            sa.ForeignKey("artifact_objects_v2.object_hash"),
            nullable=True,
        ),
        sa.Column("local_provenance", sa.Text(), nullable=True),
        sa.Column("document_identity", sa.String(), nullable=True),
        sa.Column("source_uri", sa.Text(), nullable=True),
        sa.Column("source_locator", sa.Text(), nullable=True),
        sa.Column("excerpt", sa.Text(), nullable=True),
        sa.Column("page", sa.Integer(), nullable=True),
        sa.Column("measurement_method", sa.Text(), nullable=True),
        sa.Column("measurement_unit", sa.String(), nullable=True),
        sa.Column("observed_at", _utc_type(), nullable=True),
        sa.Column("recorded_observation", sa.Text(), nullable=True),
        sa.Column("derivation_method", sa.Text(), nullable=True),
        sa.Column("test_provenance", sa.Text(), nullable=True),
        sa.Column("limitations_json", sa.JSON(), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.UniqueConstraint("revision_id", "id", name="uq_evidence_revision_id"),
        sa.CheckConstraint(_uuid_check("id"), name="ck_evidence_uuid4"),
        sa.CheckConstraint(
            "evidence_class IN ('manufacturer_document','private_upload',"
            "'user_measurement','derived_claim','validation_observation')",
            name="ck_evidence_class",
        ),
        sa.CheckConstraint(
            "source_authority IN ('manufacturer','supplier','private_provider',"
            "'user','prometheus_derivation','validation_activity',"
            "'synthetic_fixture','unknown')",
            name="ck_evidence_source_authority",
        ),
        sa.CheckConstraint(
            "physical_validation_status IN ('unvalidated','component_validated',"
            "'system_validated')",
            name="ck_evidence_physical_status",
        ),
        sa.CheckConstraint(
            "extraction_confidence IS NULL OR "
            "(extraction_confidence >= 0 AND extraction_confidence <= 1)",
            name="ck_evidence_extraction_confidence",
        ),
        sa.CheckConstraint(
            "(source_locator IS NULL AND excerpt IS NULL AND page IS NULL) OR "
            "(source_locator IS NOT NULL AND excerpt IS NOT NULL)",
            name="ck_evidence_locator_excerpt",
        ),
        sa.CheckConstraint(
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
        *_utc_checks("observed_at", "created_at"),
    )
    op.create_table(
        "claim_evidence_links_v2",
        sa.Column("revision_id", sa.String(), primary_key=True),
        sa.Column("claim_id", sa.String(36), primary_key=True),
        sa.Column("evidence_id", sa.String(36), primary_key=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_claim_link_same_revision_claim",
        ),
        sa.ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_claim_link_same_revision_evidence",
        ),
        *_utc_checks("created_at"),
    )
    op.create_table(
        "evidence_parent_claims_v2",
        sa.Column("revision_id", sa.String(), primary_key=True),
        sa.Column("evidence_id", sa.String(36), primary_key=True),
        sa.Column("parent_claim_id", sa.String(36), primary_key=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_same_revision_evidence",
        ),
        sa.ForeignKeyConstraint(
            ["revision_id", "parent_claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_evidence_parent_same_revision_claim",
        ),
        *_utc_checks("created_at"),
    )
    op.create_table(
        "evidence_parent_evidence_v2",
        sa.Column("revision_id", sa.String(), primary_key=True),
        sa.Column("evidence_id", sa.String(36), primary_key=True),
        sa.Column("parent_evidence_id", sa.String(36), primary_key=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_evidence_child",
        ),
        sa.ForeignKeyConstraint(
            ["revision_id", "parent_evidence_id"],
            ["evidence_records_v2.revision_id", "evidence_records_v2.id"],
            name="fk_evidence_parent_evidence_parent",
        ),
        sa.CheckConstraint(
            "evidence_id <> parent_evidence_id", name="ck_evidence_not_own_parent"
        ),
        *_utc_checks("created_at"),
    )
    op.create_table(
        "claim_selections_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("revision_id", sa.String(), nullable=False),
        sa.Column("slot_id", sa.String(36), nullable=False),
        sa.Column("claim_id", sa.String(36), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "slot_id", "claim_id"],
            [
                "candidate_claims_v2.revision_id",
                "candidate_claims_v2.slot_id",
                "candidate_claims_v2.id",
            ],
            name="fk_selection_same_revision_slot_claim",
        ),
        sa.UniqueConstraint(
            "revision_id", "slot_id", name="uq_selection_revision_slot"
        ),
        sa.CheckConstraint(_uuid_check("id"), name="ck_selection_uuid4"),
        *_utc_checks("created_at", "updated_at"),
    )
    op.create_table(
        "claim_review_events_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("revision_id", sa.String(), nullable=False),
        sa.Column("claim_id", sa.String(36), nullable=False),
        sa.Column("claim_fingerprint", sa.String(71), nullable=False),
        sa.Column("decision", sa.String(), nullable=False),
        sa.Column("reviewed_by", sa.String(), nullable=False),
        sa.Column("note", sa.Text(), nullable=False),
        sa.Column("reviewed_at", _utc_type(), nullable=False),
        sa.Column("applied_draft_version", sa.Integer(), nullable=False),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.ForeignKeyConstraint(
            ["revision_id", "claim_id"],
            ["candidate_claims_v2.revision_id", "candidate_claims_v2.id"],
            name="fk_review_same_revision_claim",
        ),
        sa.UniqueConstraint("revision_id", "id", name="uq_review_revision_id"),
        sa.UniqueConstraint(
            "claim_id", "applied_draft_version", name="uq_review_claim_version"
        ),
        sa.CheckConstraint(_uuid_check("id"), name="ck_review_uuid4"),
        sa.CheckConstraint(
            "decision IN ('accepted','rejected','ambiguous')",
            name="ck_review_decision",
        ),
        sa.CheckConstraint("applied_draft_version >= 1", name="ck_review_version"),
        sa.CheckConstraint(
            _hash_check("claim_fingerprint"),
            name="ck_review_fingerprint",
        ),
        *_utc_checks("reviewed_at", "created_at"),
    )
    op.create_table(
        "capability_gates_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=False,
        ),
        sa.Column("capability_id", sa.String(), nullable=False),
        sa.Column("phase", sa.String(), nullable=False),
        sa.Column("required_review_type", sa.String(), nullable=False),
        sa.Column("state", sa.String(), nullable=False),
        sa.Column("satisfying_references_json", sa.JSON(), nullable=False),
        sa.Column("reason", sa.Text(), nullable=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.UniqueConstraint("revision_id", "id", name="uq_gate_revision_id"),
        sa.CheckConstraint(_uuid_check("id"), name="ck_gate_uuid4"),
        sa.CheckConstraint("phase IN ('publication','execution')", name="ck_gate_phase"),
        sa.CheckConstraint(
            "required_review_type IN ('component_identity','source_artifact',"
            "'claim_selection','claim_review','package_consumer')",
            name="ck_gate_review_type",
        ),
        sa.CheckConstraint(
            "state IN ('pending','satisfied','blocked')", name="ck_gate_state"
        ),
        *_utc_checks("created_at", "updated_at"),
    )


def _create_job_tables() -> None:
    op.create_table(
        "fixture_ingestion_jobs_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("fixture_id", sa.String(), nullable=False),
        sa.Column("idempotency_key", sa.String(), nullable=False, unique=True),
        sa.Column("status", sa.String(), nullable=False),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=True,
        ),
        sa.Column(
            "artifact_hash",
            sa.String(71),
            sa.ForeignKey("artifact_objects_v2.object_hash"),
            nullable=True,
        ),
        sa.Column("error_code", sa.String(), nullable=True),
        sa.Column("error_message", sa.Text(), nullable=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.CheckConstraint(_uuid_check("id"), name="ck_ingestion_job_uuid4"),
        sa.CheckConstraint(
            "status IN ('queued','running','succeeded','failed','cancelled','timed_out')",
            name="ck_ingestion_status",
        ),
        sa.CheckConstraint(
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
        *_utc_checks("created_at", "updated_at"),
    )
    op.create_table(
        "publication_requests",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("operation", sa.String(), nullable=False),
        sa.Column("idempotency_key", sa.String(), nullable=False),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=False,
        ),
        sa.Column("request_fingerprint", sa.String(71), nullable=False),
        sa.Column("state", sa.String(), nullable=False),
        sa.Column("response_status", sa.Integer(), nullable=True),
        sa.Column("response_body", sa.LargeBinary(), nullable=True),
        sa.Column("response_headers_json", sa.JSON(), nullable=True),
        sa.Column(
            "published_object_hash",
            sa.String(71),
            sa.ForeignKey("published_objects.object_hash"),
            nullable=True,
        ),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.UniqueConstraint(
            "operation", "idempotency_key", name="uq_publication_operation_key"
        ),
        sa.CheckConstraint(_uuid_check("id"), name="ck_publication_request_uuid4"),
        sa.CheckConstraint("operation = 'publication'", name="ck_publication_operation"),
        sa.CheckConstraint(
            "state IN ('in_progress','succeeded','terminal_failure')",
            name="ck_publication_state",
        ),
        sa.CheckConstraint(
            _hash_check("request_fingerprint"),
            name="ck_publication_fingerprint",
        ),
        sa.CheckConstraint(
            "(state = 'in_progress' AND response_status IS NULL AND response_body "
            "IS NULL AND response_headers_json IS NULL AND published_object_hash "
            "IS NULL) OR (state = 'succeeded' AND response_status IS NOT NULL AND "
            "response_body IS NOT NULL AND response_headers_json IS NOT NULL AND "
            "published_object_hash IS NOT NULL) OR (state = 'terminal_failure' AND "
            "response_status IS NOT NULL AND response_body IS NOT NULL AND "
            "response_headers_json IS NOT NULL AND published_object_hash IS NULL)",
            name="ck_publication_state_shape",
        ),
        *_utc_checks("created_at", "updated_at"),
    )


def _sqlite_trigger(name: str, timing: str, table: str, when: str, message: str) -> None:
    op.execute(
        f"CREATE TRIGGER {name} {timing} ON {table} WHEN {when} "
        f"BEGIN SELECT RAISE(ABORT, '{message}'); END"
    )


def _install_sqlite_triggers() -> None:
    v2_marker = (
        "(NEW.contract_schema_id IS NOT NULL OR "
        "NEW.contract_schema_version IS NOT NULL OR NEW.draft_version IS NOT NULL "
        "OR NEW.publication_integrity IN ('v2_draft','sealed_v2') OR "
        "NEW.published_object_hash IS NOT NULL)"
    )
    valid_v2 = (
        f"coalesce(NEW.contract_schema_id,'')='{SCHEMA_ID}' AND "
        f"coalesce(NEW.contract_schema_version,'')='{SCHEMA_VERSION}' AND "
        f"({_uuid_check('NEW.id')}) AND NEW.draft_version IS NOT NULL AND "
        "NEW.draft_version >= 0 AND ((NEW.publication_integrity='v2_draft' AND "
        "NEW.status='draft' AND NEW.published_at IS NULL AND NEW.content_hash IS "
        "NULL AND NEW.published_object_hash IS NULL) OR ("
        "NEW.publication_integrity='sealed_v2' AND NEW.status='published' AND "
        "NEW.published_at IS NOT NULL AND NEW.content_hash IS NOT NULL AND "
        "NEW.content_hash=NEW.published_object_hash AND EXISTS (SELECT 1 FROM "
        "published_objects WHERE object_hash=NEW.published_object_hash)))"
    )
    _sqlite_trigger(
        "trg_revision_v2_shape_insert",
        "BEFORE INSERT",
        "component_revisions",
        f"{v2_marker} AND NOT ({valid_v2})",
        "invalid v2 revision state",
    )
    _sqlite_trigger(
        "trg_revision_v2_shape_update",
        "BEFORE UPDATE",
        "component_revisions",
        f"(OLD.contract_schema_id IS NOT NULL OR {v2_marker}) AND NOT ({valid_v2})",
        "invalid v2 revision state",
    )
    for action in ("INSERT", "UPDATE"):
        _sqlite_trigger(
            f"trg_revision_publication_state_{action.lower()}",
            f"BEFORE {action}",
            "component_revisions",
            "NEW.publication_integrity IS NOT NULL AND "
            "NEW.publication_integrity NOT IN "
            "('legacy_unsealed','v2_draft','sealed_v2')",
            "invalid publication integrity state",
        )
    revision_child_tables = (
        "parameter_slots_v2",
        "candidate_claims_v2",
        "evidence_records_v2",
        "claim_evidence_links_v2",
        "evidence_parent_claims_v2",
        "evidence_parent_evidence_v2",
        "claim_selections_v2",
        "claim_review_events_v2",
        "capability_gates_v2",
    )
    for table in revision_child_tables:
        _sqlite_trigger(
            f"trg_{table}_draft_insert",
            "BEFORE INSERT",
            table,
            "coalesce((SELECT publication_integrity FROM component_revisions "
            "WHERE id=NEW.revision_id),'')<>'v2_draft'",
            "v2 child insertion requires a draft revision",
        )
        _sqlite_trigger(
            f"trg_{table}_draft_update",
            "BEFORE UPDATE",
            table,
            "coalesce((SELECT publication_integrity FROM component_revisions "
            "WHERE id=OLD.revision_id),'')<>'v2_draft' OR "
            "coalesce((SELECT publication_integrity FROM component_revisions "
            "WHERE id=NEW.revision_id),'')<>'v2_draft'",
            "v2 child update requires a draft revision",
        )
        _sqlite_trigger(
            f"trg_{table}_draft_delete",
            "BEFORE DELETE",
            table,
            "coalesce((SELECT publication_integrity FROM component_revisions "
            "WHERE id=OLD.revision_id),'')<>'v2_draft'",
            "v2 child deletion requires a draft revision",
        )
    _sqlite_trigger(
        "trg_revision_published_at_utc_insert",
        "BEFORE INSERT",
        "component_revisions",
        "NEW.published_at IS NOT NULL AND (substr(NEW.published_at,-1,1)<>'Z' "
        "OR datetime(NEW.published_at) IS NULL)",
        "invalid UTC publication timestamp",
    )
    _sqlite_trigger(
        "trg_revision_published_at_utc_update",
        "BEFORE UPDATE",
        "component_revisions",
        "NEW.published_at IS NOT NULL AND (substr(NEW.published_at,-1,1)<>'Z' "
        "OR datetime(NEW.published_at) IS NULL)",
        "invalid UTC publication timestamp",
    )
    _sqlite_trigger(
        "trg_sealed_revision_update",
        "BEFORE UPDATE",
        "component_revisions",
        "OLD.publication_integrity='sealed_v2'",
        "sealed v2 revision is immutable",
    )
    _sqlite_trigger(
        "trg_sealed_revision_delete",
        "BEFORE DELETE",
        "component_revisions",
        "OLD.publication_integrity='sealed_v2'",
        "sealed v2 revision is immutable",
    )
    _sqlite_trigger(
        "trg_claim_begins_unfinalized",
        "BEFORE INSERT",
        "candidate_claims_v2",
        "NEW.finalized<>0 OR NEW.fingerprint IS NOT NULL",
        "candidate claim must begin unfinalized",
    )
    _sqlite_trigger(
        "trg_claim_finalize_once",
        "BEFORE UPDATE",
        "candidate_claims_v2",
        "NOT (OLD.finalized=0 AND OLD.fingerprint IS NULL AND NEW.finalized=1 "
        "AND NEW.fingerprint IS NOT NULL AND NEW.id IS OLD.id AND "
        "NEW.revision_id IS OLD.revision_id AND NEW.slot_id IS OLD.slot_id AND "
        "NEW.value_state IS OLD.value_state AND NEW.value_json IS OLD.value_json "
        "AND NEW.unit IS OLD.unit AND NEW.original_value IS OLD.original_value "
        "AND NEW.original_unit IS OLD.original_unit AND NEW.reason IS OLD.reason "
        "AND NEW.validity_conditions_json IS OLD.validity_conditions_json AND "
        "NEW.provenance IS OLD.provenance AND NEW.created_at IS OLD.created_at)",
        "candidate claim permits only one finalization transition",
    )
    _sqlite_trigger(
        "trg_claim_delete",
        "BEFORE DELETE",
        "candidate_claims_v2",
        "1",
        "candidate claim is immutable",
    )
    _sqlite_trigger(
        "trg_claim_link_insert",
        "BEFORE INSERT",
        "claim_evidence_links_v2",
        "coalesce((SELECT finalized FROM candidate_claims_v2 WHERE "
        "revision_id=NEW.revision_id AND id=NEW.claim_id),1)<>0",
        "evidence links require an unfinalized claim",
    )
    for action in ("UPDATE", "DELETE"):
        _sqlite_trigger(
            f"trg_claim_link_{action.lower()}",
            f"BEFORE {action}",
            "claim_evidence_links_v2",
            "1",
            "claim evidence link is immutable",
        )
    for table in ("evidence_parent_claims_v2", "evidence_parent_evidence_v2"):
        for action in ("UPDATE", "DELETE"):
            _sqlite_trigger(
                f"trg_{table}_{action.lower()}",
                f"BEFORE {action}",
                table,
                "1",
                "evidence parent link is immutable",
            )
    for action in ("INSERT", "UPDATE"):
        _sqlite_trigger(
            f"trg_selection_finalized_{action.lower()}",
            f"BEFORE {action}",
            "claim_selections_v2",
            "coalesce((SELECT finalized FROM candidate_claims_v2 WHERE "
            "revision_id=NEW.revision_id AND slot_id=NEW.slot_id AND "
            "id=NEW.claim_id),0)<>1",
            "selection requires a finalized same-revision claim",
        )
    _sqlite_trigger(
        "trg_review_finalized_insert",
        "BEFORE INSERT",
        "claim_review_events_v2",
        "coalesce((SELECT finalized FROM candidate_claims_v2 WHERE "
        "revision_id=NEW.revision_id AND id=NEW.claim_id),0)<>1 OR "
        "coalesce((SELECT fingerprint FROM candidate_claims_v2 WHERE "
        "revision_id=NEW.revision_id AND id=NEW.claim_id),'')<>NEW.claim_fingerprint",
        "review requires the finalized claim fingerprint",
    )
    immutable_tables = (
        "artifact_objects_v2",
        "evidence_records_v2",
        "claim_review_events_v2",
        "published_objects",
    )
    for table in immutable_tables:
        for action in ("UPDATE", "DELETE"):
            _sqlite_trigger(
                f"trg_{table}_{action.lower()}",
                f"BEFORE {action}",
                table,
                "1",
                f"{table} is immutable",
            )


def _install_postgresql_triggers() -> None:
    # PostgreSQL types enforce UTC awareness. These trigger functions supply the
    # same mutation semantics as the SQLite development target.
    op.execute(
        """
        CREATE FUNCTION prometheus_reject_v2_mutation() RETURNS trigger AS $$
        BEGIN RAISE EXCEPTION 'immutable Program 01A v2 row'; END;
        $$ LANGUAGE plpgsql
        """
    )
    for table in (
        "artifact_objects_v2",
        "evidence_records_v2",
        "claim_review_events_v2",
        "published_objects",
        "claim_evidence_links_v2",
        "evidence_parent_claims_v2",
        "evidence_parent_evidence_v2",
    ):
        op.execute(
            f"CREATE TRIGGER trg_{table}_immutable BEFORE UPDATE OR DELETE ON {table} "
            "FOR EACH ROW EXECUTE FUNCTION prometheus_reject_v2_mutation()"
        )
    op.execute(
        """
        CREATE FUNCTION prometheus_claim_begins_unfinalized() RETURNS trigger AS $$
        BEGIN
          IF NEW.finalized IS NOT FALSE OR NEW.fingerprint IS NOT NULL THEN
            RAISE EXCEPTION 'candidate claim must begin unfinalized';
          END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    op.execute(
        "CREATE TRIGGER trg_claim_begins_unfinalized BEFORE INSERT ON "
        "candidate_claims_v2 FOR EACH ROW EXECUTE FUNCTION "
        "prometheus_claim_begins_unfinalized()"
    )
    op.execute(
        """
        CREATE FUNCTION prometheus_claim_finalize_once() RETURNS trigger AS $$
        BEGIN
          IF NOT (OLD.finalized IS FALSE AND OLD.fingerprint IS NULL AND
                  NEW.finalized IS TRUE AND NEW.fingerprint IS NOT NULL AND
                  ROW(NEW.id,NEW.revision_id,NEW.slot_id,NEW.value_state,
                      NEW.unit,NEW.original_value,NEW.original_unit,NEW.reason,
                      NEW.provenance,NEW.created_at)
                  IS NOT DISTINCT FROM
                  ROW(OLD.id,OLD.revision_id,OLD.slot_id,OLD.value_state,
                      OLD.unit,OLD.original_value,OLD.original_unit,OLD.reason,
                      OLD.provenance,OLD.created_at) AND
                  NEW.value_json::jsonb IS NOT DISTINCT FROM OLD.value_json::jsonb AND
                  NEW.validity_conditions_json::jsonb IS NOT DISTINCT FROM
                      OLD.validity_conditions_json::jsonb) THEN
            RAISE EXCEPTION 'candidate claim permits only one finalization transition';
          END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    op.execute(
        "CREATE TRIGGER trg_claim_finalize_once BEFORE UPDATE ON candidate_claims_v2 "
        "FOR EACH ROW EXECUTE FUNCTION prometheus_claim_finalize_once()"
    )
    op.execute(
        "CREATE TRIGGER trg_claim_delete BEFORE DELETE ON candidate_claims_v2 "
        "FOR EACH ROW EXECUTE FUNCTION prometheus_reject_v2_mutation()"
    )
    op.execute(
        """
        CREATE FUNCTION prometheus_claim_link_guard() RETURNS trigger AS $$
        BEGIN
          IF NOT EXISTS (SELECT 1 FROM candidate_claims_v2 WHERE
              revision_id=NEW.revision_id AND id=NEW.claim_id AND finalized IS FALSE) THEN
            RAISE EXCEPTION 'evidence links require an unfinalized claim';
          END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    op.execute(
        "CREATE TRIGGER trg_claim_link_insert BEFORE INSERT ON claim_evidence_links_v2 "
        "FOR EACH ROW EXECUTE FUNCTION prometheus_claim_link_guard()"
    )
    op.execute(
        """
        CREATE FUNCTION prometheus_finalized_claim_guard() RETURNS trigger AS $$
        BEGIN
          IF NOT EXISTS (SELECT 1 FROM candidate_claims_v2 WHERE
              revision_id=NEW.revision_id AND id=NEW.claim_id AND finalized IS TRUE) THEN
            RAISE EXCEPTION 'operation requires a finalized claim';
          END IF;
          IF TG_TABLE_NAME='claim_review_events_v2' AND NOT EXISTS (
              SELECT 1 FROM candidate_claims_v2 WHERE revision_id=NEW.revision_id
              AND id=NEW.claim_id AND
                  fingerprint=(to_jsonb(NEW)->>'claim_fingerprint')) THEN
            RAISE EXCEPTION 'review fingerprint mismatch';
          END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    op.execute(
        "CREATE TRIGGER trg_selection_finalized BEFORE INSERT OR UPDATE ON "
        "claim_selections_v2 FOR EACH ROW EXECUTE FUNCTION "
        "prometheus_finalized_claim_guard()"
    )
    op.execute(
        "CREATE TRIGGER trg_review_finalized BEFORE INSERT ON claim_review_events_v2 "
        "FOR EACH ROW EXECUTE FUNCTION prometheus_finalized_claim_guard()"
    )
    op.execute(
        """
        CREATE FUNCTION prometheus_draft_revision_guard() RETURNS trigger AS $$
        DECLARE old_state text; new_state text;
        BEGIN
          IF TG_OP <> 'INSERT' THEN
            SELECT publication_integrity INTO old_state FROM component_revisions
              WHERE id=OLD.revision_id;
            IF old_state IS DISTINCT FROM 'v2_draft' THEN
              RAISE EXCEPTION 'v2 child mutation requires a draft revision';
            END IF;
          END IF;
          IF TG_OP <> 'DELETE' THEN
            SELECT publication_integrity INTO new_state FROM component_revisions
              WHERE id=NEW.revision_id;
            IF new_state IS DISTINCT FROM 'v2_draft' THEN
              RAISE EXCEPTION 'v2 child mutation requires a draft revision';
            END IF;
          END IF;
          IF TG_OP='DELETE' THEN RETURN OLD; END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    for table in (
        "parameter_slots_v2",
        "candidate_claims_v2",
        "evidence_records_v2",
        "claim_evidence_links_v2",
        "evidence_parent_claims_v2",
        "evidence_parent_evidence_v2",
        "claim_selections_v2",
        "claim_review_events_v2",
        "capability_gates_v2",
    ):
        op.execute(
            f"CREATE TRIGGER trg_{table}_draft BEFORE INSERT OR UPDATE OR DELETE "
            f"ON {table} FOR EACH ROW EXECUTE FUNCTION "
            "prometheus_draft_revision_guard()"
        )
    op.execute(
        """
        CREATE FUNCTION prometheus_revision_guard() RETURNS trigger AS $$
        DECLARE v2_row boolean;
        BEGIN
          IF TG_OP <> 'INSERT' AND OLD.publication_integrity='sealed_v2' THEN
            RAISE EXCEPTION 'sealed v2 revision is immutable';
          END IF;
          IF NEW.publication_integrity IS NOT NULL AND
             NEW.publication_integrity NOT IN
                 ('legacy_unsealed','v2_draft','sealed_v2') THEN
            RAISE EXCEPTION 'invalid publication integrity state';
          END IF;
          v2_row := NEW.contract_schema_id IS NOT NULL OR
                    NEW.contract_schema_version IS NOT NULL OR
                    NEW.draft_version IS NOT NULL OR
                    NEW.publication_integrity IN ('v2_draft','sealed_v2') OR
                    NEW.published_object_hash IS NOT NULL;
          IF TG_OP='UPDATE' AND OLD.contract_schema_id IS NOT NULL THEN
            v2_row := TRUE;
          END IF;
          IF v2_row AND (
              NEW.contract_schema_id='urn:prometheus:schema:execution-component:2.0.0'
              AND NEW.contract_schema_version='2.0.0'
              AND NEW.id ~ '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'
              AND NEW.draft_version >= 0 AND
              ((NEW.publication_integrity='v2_draft' AND NEW.status='draft' AND
                NEW.published_at IS NULL AND NEW.content_hash IS NULL AND
                NEW.published_object_hash IS NULL) OR
               (NEW.publication_integrity='sealed_v2' AND NEW.status='published' AND
                NEW.published_at IS NOT NULL AND NEW.content_hash IS NOT NULL AND
                NEW.content_hash=NEW.published_object_hash))) IS NOT TRUE THEN
            RAISE EXCEPTION 'invalid v2 revision state';
          END IF;
          RETURN NEW;
        END; $$ LANGUAGE plpgsql
        """
    )
    op.execute(
        "CREATE TRIGGER trg_revision_guard BEFORE INSERT OR UPDATE ON "
        "component_revisions FOR EACH ROW EXECUTE FUNCTION prometheus_revision_guard()"
    )
    op.execute(
        "CREATE TRIGGER trg_revision_delete BEFORE DELETE ON component_revisions "
        "FOR EACH ROW WHEN (OLD.publication_integrity='sealed_v2') EXECUTE FUNCTION "
        "prometheus_reject_v2_mutation()"
    )


def upgrade() -> None:
    if _is_sqlite():
        op.get_bind().exec_driver_sql("PRAGMA foreign_keys=ON")
    _normalize_legacy_publication_timestamps()
    _create_object_tables()
    _extend_component_revisions()
    _create_claim_graph_tables()
    _create_job_tables()
    if _is_sqlite():
        _install_sqlite_triggers()
    elif op.get_bind().dialect.name == "postgresql":
        _install_postgresql_triggers()


def _drop_postgresql_functions() -> None:
    for function in (
        "prometheus_revision_guard",
        "prometheus_draft_revision_guard",
        "prometheus_finalized_claim_guard",
        "prometheus_claim_link_guard",
        "prometheus_claim_finalize_once",
        "prometheus_claim_begins_unfinalized",
        "prometheus_reject_v2_mutation",
    ):
        op.execute(f"DROP FUNCTION IF EXISTS {function}() CASCADE")


def downgrade() -> None:
    connection = op.get_bind()
    if connection.execute(
        sa.text(
            "SELECT COUNT(*) FROM component_revisions "
            "WHERE publication_integrity='sealed_v2'"
        )
    ).scalar_one():
        raise RuntimeError("cannot downgrade while a sealed v2 revision exists")
    if connection.dialect.name == "postgresql":
        _drop_postgresql_functions()
    elif _is_sqlite():
        for trigger in (
            "trg_revision_v2_shape_insert",
            "trg_revision_v2_shape_update",
            "trg_revision_published_at_utc_insert",
            "trg_revision_published_at_utc_update",
            "trg_revision_publication_state_insert",
            "trg_revision_publication_state_update",
            "trg_sealed_revision_update",
            "trg_sealed_revision_delete",
        ):
            op.execute(f"DROP TRIGGER IF EXISTS {trigger}")

    for table in (
        "publication_requests",
        "fixture_ingestion_jobs_v2",
        "capability_gates_v2",
        "claim_review_events_v2",
        "claim_selections_v2",
        "evidence_parent_evidence_v2",
        "evidence_parent_claims_v2",
        "claim_evidence_links_v2",
        "evidence_records_v2",
        "candidate_claims_v2",
        "parameter_slots_v2",
    ):
        op.drop_table(table)

    if connection.dialect.name == "postgresql":
        op.alter_column(
            "component_revisions",
            "published_at",
            existing_type=sa.DateTime(timezone=True),
            type_=sa.String(),
            postgresql_using="published_at::text",
            existing_nullable=True,
        )
    if not _is_sqlite():
        op.drop_constraint(
            "fk_revision_published_object",
            "component_revisions",
            type_="foreignkey",
        )
    for column in (
        "published_object_hash",
        "publication_integrity",
        "contract_schema_version",
        "contract_schema_id",
        "draft_version",
    ):
        op.drop_column("component_revisions", column)

    op.drop_table("published_objects")
    op.drop_table("artifact_objects_v2")
