from decimal import Decimal
from datetime import datetime

from sqlalchemy import JSON, ForeignKey, Integer, Numeric, String, Text, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from .db_types import UtcTimestamp
from .models import Record


class Manufacturer(Record):
    __tablename__ = "manufacturers"
    name: Mapped[str] = mapped_column(String, unique=True)
    normalized_name: Mapped[str] = mapped_column(String, index=True)


class Component(Record):
    __tablename__ = "components"
    manufacturer_id: Mapped[str] = mapped_column(ForeignKey("manufacturers.id"))
    part_number: Mapped[str] = mapped_column(String)
    normalized_part_number: Mapped[str] = mapped_column(String, index=True)
    family: Mapped[str] = mapped_column(String, default="")
    model_class: Mapped[str] = mapped_column(String)
    __table_args__ = (
        UniqueConstraint("manufacturer_id", "part_number", name="uq_component_mfr_part"),
    )


class ComponentRevision(Record):
    __tablename__ = "component_revisions"
    component_id: Mapped[str] = mapped_column(ForeignKey("components.id"))
    revision: Mapped[str] = mapped_column(String)
    parent_revision_id: Mapped[str | None] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=True
    )
    certification_tier: Mapped[str] = mapped_column(String, default="provisional")
    status: Mapped[str] = mapped_column(String, default="draft")
    published_at: Mapped[datetime | None] = mapped_column(
        UtcTimestamp(), nullable=True
    )
    content_hash: Mapped[str | None] = mapped_column(String, nullable=True)
    draft_version: Mapped[int | None] = mapped_column(Integer, nullable=True)
    contract_schema_id: Mapped[str | None] = mapped_column(String, nullable=True)
    contract_schema_version: Mapped[str | None] = mapped_column(String, nullable=True)
    publication_integrity: Mapped[str | None] = mapped_column(String, nullable=True)
    published_object_hash: Mapped[str | None] = mapped_column(String, nullable=True)
    supported_recipes: Mapped[list] = mapped_column(
        "supported_recipes_json", JSON, default=list
    )
    missing_information: Mapped[list] = mapped_column(
        "missing_information_json", JSON, default=list
    )
    limitations: Mapped[list] = mapped_column("limitations_json", JSON, default=list)
    __table_args__ = (
        UniqueConstraint("component_id", "revision", name="uq_component_revision"),
    )


class ComponentParameter(Record):
    __tablename__ = "component_parameters"
    revision_id: Mapped[str] = mapped_column(ForeignKey("component_revisions.id"))
    field_name: Mapped[str] = mapped_column(String)
    quantity: Mapped[str] = mapped_column(String)
    dimension: Mapped[str] = mapped_column(String)
    value: Mapped[dict] = mapped_column("value_json", JSON)
    unit: Mapped[str] = mapped_column("unit_si", String)
    original_value: Mapped[str] = mapped_column(String)
    original_unit: Mapped[str] = mapped_column(String)
    validity_conditions: Mapped[list] = mapped_column(
        "validity_conditions_json", JSON, default=list
    )
    __table_args__ = (
        UniqueConstraint("revision_id", "field_name", name="uq_revision_parameter"),
    )


class SourceDocument(Record):
    __tablename__ = "source_documents"
    source_url: Mapped[str] = mapped_column(String)
    title: Mapped[str] = mapped_column(String)
    document_hash: Mapped[str] = mapped_column(String, unique=True)
    rights_status: Mapped[str] = mapped_column(String, default="link_only")
    content_type: Mapped[str] = mapped_column(String)


class EvidenceRecord(Record):
    __tablename__ = "evidence_records"
    parameter_id: Mapped[str] = mapped_column(ForeignKey("component_parameters.id"))
    source_document_id: Mapped[str] = mapped_column(ForeignKey("source_documents.id"))
    source_locator: Mapped[str] = mapped_column(String)
    source_excerpt: Mapped[str] = mapped_column(Text)
    evidence_class: Mapped[str] = mapped_column(String)
    confidence: Mapped[Decimal | None] = mapped_column(Numeric(4, 3), nullable=True)
    extraction_method: Mapped[str] = mapped_column(String)
    review_status: Mapped[str] = mapped_column(String, default="pending")
    reviewed_by: Mapped[str | None] = mapped_column(String, nullable=True)
    reviewed_at: Mapped[str | None] = mapped_column(String, nullable=True)
    review_note: Mapped[str | None] = mapped_column(String, nullable=True)


class ResearchJob(Record):
    __tablename__ = "research_jobs"
    idempotency_key: Mapped[str] = mapped_column(String, unique=True)
    status: Mapped[str] = mapped_column(String)
    query: Mapped[str] = mapped_column(Text)
    revision_id: Mapped[str | None] = mapped_column(
        ForeignKey("component_revisions.id"), nullable=True
    )
    provider: Mapped[str] = mapped_column(String, default="fixture")
    schema_version: Mapped[str] = mapped_column(String, default="1.0.0")


class ResearchJobEvent(Record):
    __tablename__ = "research_job_events"
    job_id: Mapped[str] = mapped_column(ForeignKey("research_jobs.id"))
    stage: Mapped[str] = mapped_column(String)
    sequence: Mapped[int] = mapped_column(Integer)
    message: Mapped[str] = mapped_column(String)


class ProjectComponentBinding(Record):
    __tablename__ = "project_component_bindings"
    project_id: Mapped[str] = mapped_column(String)
    cad_entity_id: Mapped[str] = mapped_column(String)
    revision_id: Mapped[str] = mapped_column(ForeignKey("component_revisions.id"))
    __table_args__ = (
        UniqueConstraint("project_id", "cad_entity_id", name="uq_project_cad_binding"),
    )
