"""Historical v1 package reconstruction; v2 production code must not import it."""

import hashlib
import json
from typing import Any

from sqlalchemy import select
from sqlalchemy.orm import Session

from .contracts_v1 import ExecutionComponentPackage, ExecutionComponentPayload
from .models_v1 import (
    Component,
    ComponentParameter,
    ComponentRevision,
    EvidenceRecord,
    Manufacturer,
    SourceDocument,
)


class ExecutionPackageError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def canonical_json_bytes(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def content_hash(value: dict[str, Any]) -> str:
    payload = dict(value)
    payload.pop("content_hash", None)
    digest = hashlib.sha256(canonical_json_bytes(payload)).hexdigest()
    return f"sha256:{digest}"


def build_execution_component_payload(
    revision: ComponentRevision, db: Session
) -> dict[str, Any]:
    if revision.status != "published" or revision.published_at is None:
        raise ExecutionPackageError(
            "revision_not_published",
            "Only published component revisions have execution packages.",
        )

    component = db.get(Component, revision.component_id)
    if component is None:
        raise ExecutionPackageError(
            "execution_package_invalid",
            "The revision references a missing component.",
        )
    manufacturer = db.get(Manufacturer, component.manufacturer_id)
    if manufacturer is None:
        raise ExecutionPackageError(
            "execution_package_invalid",
            "The component references a missing manufacturer.",
        )

    parameters = db.scalars(
        select(ComponentParameter)
        .where(ComponentParameter.revision_id == revision.id)
        .order_by(ComponentParameter.field_name)
    ).all()
    if not parameters:
        raise ExecutionPackageError(
            "execution_package_evidence_incomplete",
            "An execution component requires at least one reviewed parameter.",
        )

    evidence_records = db.scalars(
        select(EvidenceRecord)
        .join(ComponentParameter)
        .where(ComponentParameter.revision_id == revision.id)
        .order_by(EvidenceRecord.id)
    ).all()
    evidence_by_parameter: dict[str, list[EvidenceRecord]] = {
        parameter.id: [] for parameter in parameters
    }
    for record in evidence_records:
        evidence_by_parameter[record.parameter_id].append(record)
    if any(
        not evidence_by_parameter[parameter.id]
        or any(
            record.review_status != "accepted"
            for record in evidence_by_parameter[parameter.id]
        )
        for parameter in parameters
    ):
        raise ExecutionPackageError(
            "execution_package_evidence_incomplete",
            "Every execution parameter requires explicitly accepted evidence.",
        )

    source_document_ids = {
        record.source_document_id for record in evidence_records
    }
    source_documents = {
        document.id: document
        for document in db.scalars(
            select(SourceDocument).where(SourceDocument.id.in_(source_document_ids))
        ).all()
    }
    if set(source_documents) != source_document_ids:
        raise ExecutionPackageError(
            "execution_package_invalid",
            "Execution evidence references a missing source document.",
        )

    payload = {
        "schema_version": "1.0.0",
        "package_kind": "component_execution_input",
        "revision_id": revision.id,
        "component": {
            "id": component.id,
            "manufacturer": manufacturer.name,
            "part_number": component.part_number,
            "revision": revision.revision,
            "component_class": component.model_class,
        },
        "certification": {
            "tier": revision.certification_tier,
            "status": "published",
            "published_at": revision.published_at,
        },
        "parameters": [
            {
                "name": parameter.field_name,
                "quantity": parameter.quantity,
                "dimension": parameter.dimension,
                "value": parameter.value,
                "unit": parameter.unit,
                "original_value": parameter.original_value,
                "original_unit": parameter.original_unit,
                "validity_conditions": parameter.validity_conditions,
                "evidence_ids": [
                    record.id
                    for record in sorted(
                        evidence_by_parameter[parameter.id], key=lambda item: item.id
                    )
                ],
            }
            for parameter in parameters
        ],
        "evidence": [
            {
                "schema_version": "1.0.0",
                "id": record.id,
                "evidence_class": record.evidence_class,
                "source_document_id": record.source_document_id,
                "source_document_hash": source_documents[
                    record.source_document_id
                ].document_hash,
                "source_uri": source_documents[record.source_document_id].source_url,
                "source_locator": record.source_locator,
                "excerpt": record.source_excerpt,
                "confidence": record.confidence,
                "extraction_method": record.extraction_method,
                "review": {
                    "status": record.review_status,
                    "reviewed_by": record.reviewed_by,
                    "reviewed_at": record.reviewed_at,
                    "note": record.review_note,
                },
            }
            for record in evidence_records
        ],
        "supported_recipes": revision.supported_recipes,
        "missing_information": revision.missing_information,
        "limitations": revision.limitations,
        "authority": {
            "package_role": "reviewed_input",
            "engineering_decision_authority": "prometheus_cpp",
        },
    }
    validated = ExecutionComponentPayload.model_validate(payload)
    return validated.model_dump(mode="json")


def finalize_execution_component(
    payload: dict[str, Any], expected_hash: str | None = None
) -> dict[str, Any]:
    validated_payload = ExecutionComponentPayload.model_validate(payload).model_dump(
        mode="json"
    )
    calculated_hash = content_hash(validated_payload)
    if expected_hash is not None and calculated_hash != expected_hash:
        raise ExecutionPackageError(
            "execution_package_hash_mismatch",
            "The persisted revision no longer matches its published content hash.",
        )
    package = {**validated_payload, "content_hash": calculated_hash}
    return ExecutionComponentPackage.model_validate(package).model_dump(mode="json")
