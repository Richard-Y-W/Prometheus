from datetime import datetime, timezone
from collections import Counter

from fastapi import APIRouter, Depends, Header, HTTPException, Query, Response
from pydantic import ValidationError
from sqlalchemy import or_, select
from sqlalchemy.orm import Session

from .contracts_v1 import ResearchCreate, ReviewRequest
from .database import get_db
from .execution_packages import (
    ExecutionPackageError,
    build_execution_component_payload,
    canonical_json_bytes,
    finalize_execution_component,
)
from .fixture_catalog import (
    FixtureRequestError,
    fixture_error_detail,
    get_fixture,
    normalize_identity,
)
from .models_v1 import (
    Component,
    ComponentParameter,
    ComponentRevision,
    EvidenceRecord,
    Manufacturer,
    ResearchJob,
    ResearchJobEvent,
    SourceDocument,
)


router = APIRouter(prefix="/v1")


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def api_error(status_code: int, code: str, message: str, **context) -> HTTPException:
    return HTTPException(
        status_code=status_code,
        detail={"code": code, "message": message, **context},
    )


def execution_package_validation_error(error: ValidationError) -> HTTPException:
    validation_errors = [
        {
            "type": item["type"],
            "location": [str(part) for part in item["loc"]],
            "message": item["msg"],
        }
        for item in error.errors(include_url=False)
    ]
    return api_error(
        409,
        "execution_package_invalid",
        "The persisted revision cannot form a valid execution package.",
        validation_errors=validation_errors,
    )


def revision_detail(revision: ComponentRevision, db: Session) -> dict:
    component = db.get(Component, revision.component_id)
    manufacturer = db.get(Manufacturer, component.manufacturer_id)
    parameters = []
    statement = (
        select(ComponentParameter)
        .where(ComponentParameter.revision_id == revision.id)
        .order_by(ComponentParameter.field_name)
    )
    for parameter in db.scalars(statement).all():
        evidence = db.scalars(
            select(EvidenceRecord).where(EvidenceRecord.parameter_id == parameter.id)
        ).all()
        parameters.append(
            {
                "id": parameter.id,
                "field_name": parameter.field_name,
                "quantity": parameter.quantity,
                "dimension": parameter.dimension,
                "value": parameter.value,
                "unit": parameter.unit,
                "original_value": parameter.original_value,
                "original_unit": parameter.original_unit,
                "validity_conditions": parameter.validity_conditions,
                "evidence": [
                    {column.name: getattr(record, column.name) for column in record.__table__.columns}
                    for record in evidence
                ],
            }
        )
    return {
        "id": revision.id,
        "component_id": component.id,
        "manufacturer": manufacturer.name,
        "part_number": component.part_number,
        "model_class": component.model_class,
        "revision": revision.revision,
        "status": revision.status,
        "certification_tier": revision.certification_tier,
        "published_at": revision.published_at,
        "content_hash": revision.content_hash,
        "parameters": parameters,
    }


@router.get("/components/search")
def search_components(
    q: str = "",
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    db: Session = Depends(get_db),
):
    statement = select(Component)
    token = normalize_identity(q)
    if token:
        statement = statement.where(
            or_(
                Component.normalized_part_number.contains(token),
                Component.family.ilike(f"%{q}%"),
            )
        )
    rows = db.scalars(
        statement.offset((page - 1) * page_size).limit(page_size + 1)
    ).all()
    items = [
        {
            "id": component.id,
            "manufacturer": db.get(Manufacturer, component.manufacturer_id).name,
            "part_number": component.part_number,
            "family": component.family,
            "model_class": component.model_class,
        }
        for component in rows[:page_size]
    ]
    return {
        "items": items,
        "page": page,
        "page_size": page_size,
        "has_more": len(rows) > page_size,
    }


@router.get("/components/{component_id}")
def component_detail(component_id: str, db: Session = Depends(get_db)):
    component = db.get(Component, component_id)
    if not component:
        raise HTTPException(404, "component not found")
    revisions = db.scalars(
        select(ComponentRevision).where(ComponentRevision.component_id == component.id)
    ).all()
    return {
        "id": component.id,
        "manufacturer": db.get(Manufacturer, component.manufacturer_id).name,
        "part_number": component.part_number,
        "revisions": [revision_detail(revision, db) for revision in revisions],
    }


@router.get("/components/{component_id}/revisions/{revision_id}")
def get_revision(component_id: str, revision_id: str, db: Session = Depends(get_db)):
    revision = db.get(ComponentRevision, revision_id)
    if not revision or revision.component_id != component_id:
        raise HTTPException(404, "revision not found")
    return revision_detail(revision, db)


@router.post("/research-jobs", status_code=202)
def create_research_job(
    body: ResearchCreate,
    idempotency_key: str = Header(alias="Idempotency-Key"),
    db: Session = Depends(get_db),
):
    try:
        fixture = get_fixture(body.manufacturer, body.part_number, body.source_url)
    except FixtureRequestError as error:
        raise HTTPException(status_code=422, detail=fixture_error_detail(error)) from error

    existing = db.scalar(
        select(ResearchJob).where(ResearchJob.idempotency_key == idempotency_key)
    )
    if existing:
        return job_detail(existing, db)

    manufacturer = db.scalar(
        select(Manufacturer).where(
            Manufacturer.normalized_name == normalize_identity(fixture.manufacturer)
        )
    )
    if not manufacturer:
        manufacturer = Manufacturer(
            name=fixture.manufacturer,
            normalized_name=normalize_identity(fixture.manufacturer),
        )
        db.add(manufacturer)
        db.flush()

    component = db.scalar(
        select(Component).where(
            Component.manufacturer_id == manufacturer.id,
            Component.part_number == fixture.part_number,
        )
    )
    if not component:
        component = Component(
            manufacturer_id=manufacturer.id,
            part_number=fixture.part_number,
            normalized_part_number=normalize_identity(fixture.part_number),
            family=fixture.family,
            model_class=fixture.component_class,
        )
        db.add(component)
        db.flush()

    cached_revision = db.scalar(
        select(ComponentRevision).where(
            ComponentRevision.component_id == component.id,
            ComponentRevision.revision == fixture.revision,
        )
    )
    if cached_revision:
        status = "published" if cached_revision.status == "published" else "ready_for_review"
        job = ResearchJob(
            idempotency_key=idempotency_key,
            status=status,
            query=body.model_dump_json(),
            revision_id=cached_revision.id,
        )
        db.add(job)
        db.flush()
        events = [
            ("cache_lookup", "Reusable component revision found"),
            (status.replace("published", "ready_for_review"), "Cached revision ready"),
        ]
        for sequence, (stage, message) in enumerate(events, 1):
            db.add(
                ResearchJobEvent(
                    job_id=job.id,
                    stage=stage,
                    sequence=sequence,
                    message=message,
                )
            )
        db.commit()
        return job_detail(job, db)

    revision = ComponentRevision(
        component_id=component.id,
        revision=fixture.revision,
        certification_tier="provisional",
        status="draft",
        content_hash=None,
        supported_recipes=list(fixture.supported_recipes),
        missing_information=list(fixture.missing_information),
        limitations=list(fixture.limitations),
    )
    db.add(revision)
    db.flush()

    document = SourceDocument(
        source_url=fixture.source.uri,
        title=fixture.source.title,
        document_hash=fixture.source.document_hash,
        rights_status=fixture.source.rights_status,
        content_type=fixture.source.content_type,
    )
    existing_document = db.scalar(
        select(SourceDocument).where(
            SourceDocument.document_hash == document.document_hash
        )
    )
    document = existing_document or document
    if existing_document is None:
        db.add(document)
        db.flush()

    for fixture_parameter in fixture.parameters:
        parameter = ComponentParameter(
            revision_id=revision.id,
            field_name=fixture_parameter.name,
            quantity=fixture_parameter.quantity,
            dimension=fixture_parameter.dimension,
            value=fixture_parameter.value,
            unit=fixture_parameter.unit,
            original_value=fixture_parameter.original_value,
            original_unit=fixture_parameter.original_unit,
            validity_conditions=list(fixture_parameter.validity_conditions),
        )
        db.add(parameter)
        db.flush()
        db.add(
            EvidenceRecord(
                parameter_id=parameter.id,
                source_document_id=document.id,
                source_locator=f"parameters.{fixture_parameter.name}",
                source_excerpt=(
                    f"{fixture_parameter.name}: {fixture_parameter.original_value} "
                    f"{fixture_parameter.original_unit}"
                ),
                evidence_class=fixture_parameter.evidence_class,
                confidence=None,
                extraction_method="fixture_json_v1",
                review_status="pending",
            )
        )

    job = ResearchJob(
        idempotency_key=idempotency_key,
        status="ready_for_review",
        query=body.model_dump_json(),
        revision_id=revision.id,
    )
    db.add(job)
    db.flush()
    stages = [
        "identity_resolution",
        "source_acquisition",
        "extraction",
        "unit_validation",
        "conflict_detection",
        "model_compilation",
        "missing_field_analysis",
        "ready_for_review",
    ]
    for sequence, stage in enumerate(stages, 1):
        db.add(
            ResearchJobEvent(
                job_id=job.id,
                stage=stage,
                sequence=sequence,
                message=stage.replace("_", " ").title(),
            )
        )
    db.commit()
    return job_detail(job, db)


def job_detail(job: ResearchJob, db: Session) -> dict:
    events = db.scalars(
        select(ResearchJobEvent)
        .where(ResearchJobEvent.job_id == job.id)
        .order_by(ResearchJobEvent.sequence)
    ).all()
    return {
        "id": job.id,
        "status": job.status,
        "revision_id": job.revision_id,
        "provider": job.provider,
        "events": [
            {column.name: getattr(event, column.name) for column in event.__table__.columns}
            for event in events
        ],
        "candidate": (
            revision_detail(db.get(ComponentRevision, job.revision_id), db)
            if job.revision_id
            else None
        ),
    }


@router.get("/research-jobs/{job_id}")
def get_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(ResearchJob, job_id)
    if not job:
        raise HTTPException(404, "research job not found")
    return job_detail(job, db)


@router.get("/research-jobs/{job_id}/events")
def get_events(job_id: str, db: Session = Depends(get_db)):
    return get_job(job_id, db)["events"]


@router.post("/research-jobs/{job_id}/review")
def review(job_id: str, body: ReviewRequest, db: Session = Depends(get_db)):
    job = db.get(ResearchJob, job_id)
    if not job:
        raise HTTPException(404, "research job not found")
    if db.get(ComponentRevision, job.revision_id).status == "published":
        raise api_error(
            409,
            "published_revision_immutable",
            "Published component revisions are immutable.",
        )
    parameters = {
        parameter.field_name: parameter
        for parameter in db.scalars(
            select(ComponentParameter).where(
                ComponentParameter.revision_id == job.revision_id
            )
        ).all()
    }
    decision_fields = [decision.field_name for decision in body.decisions]
    duplicate_fields = sorted(
        field_name
        for field_name, count in Counter(decision_fields).items()
        if count > 1
    )
    if duplicate_fields:
        raise api_error(
            422,
            "duplicate_review_decision",
            "Each parameter requires exactly one review decision.",
            duplicate_fields=duplicate_fields,
        )
    parameter_fields = set(parameters)
    decision_field_set = set(decision_fields)
    unknown_fields = sorted(decision_field_set - parameter_fields)
    if unknown_fields:
        raise api_error(
            422,
            "unknown_review_field",
            "Review decisions contain fields that are not in this revision.",
            unknown_fields=unknown_fields,
        )
    missing_fields = sorted(parameter_fields - decision_field_set)
    if missing_fields:
        raise api_error(
            422,
            "review_decisions_incomplete",
            "Every parameter requires an explicit review decision.",
            missing_fields=missing_fields,
        )

    reviewed_at = now()
    decisions_by_field = {decision.field_name: decision for decision in body.decisions}
    for field_name, parameter in parameters.items():
        decision = decisions_by_field[field_name]
        evidence_records = db.scalars(
            select(EvidenceRecord).where(
                EvidenceRecord.parameter_id == parameter.id
            )
        ).all()
        for evidence in evidence_records:
            evidence.review_status = decision.status
            evidence.reviewed_by = body.reviewed_by
            evidence.reviewed_at = reviewed_at
            evidence.review_note = decision.note
    statuses = {decision.status for decision in body.decisions}
    if "rejected" in statuses:
        job.status = "review_rejected"
    elif "ambiguous" in statuses:
        job.status = "review_ambiguous"
    else:
        job.status = "reviewed"
    db.commit()
    return job_detail(job, db)


@router.post("/research-jobs/{job_id}/publish")
def publish(
    job_id: str,
    idempotency_key: str = Header(alias="Idempotency-Key"),
    db: Session = Depends(get_db),
):
    del idempotency_key
    job = db.get(ResearchJob, job_id)
    if not job:
        raise HTTPException(404, "research job not found")
    revision = db.get(ComponentRevision, job.revision_id)
    if revision.status == "published":
        return revision_detail(revision, db)
    parameters = db.scalars(
        select(ComponentParameter).where(ComponentParameter.revision_id == revision.id)
    ).all()
    evidence = db.scalars(
        select(EvidenceRecord)
        .join(ComponentParameter)
        .where(ComponentParameter.revision_id == revision.id)
    ).all()
    evidence_by_parameter: dict[str, list[EvidenceRecord]] = {
        parameter.id: [] for parameter in parameters
    }
    for record in evidence:
        evidence_by_parameter.setdefault(record.parameter_id, []).append(record)
    review_incomplete = not parameters or any(
        not evidence_by_parameter[parameter.id]
        or any(
            record.review_status != "accepted"
            for record in evidence_by_parameter[parameter.id]
        )
        for parameter in parameters
    )
    if review_incomplete:
        raise api_error(
            409,
            "publication_review_incomplete",
            "Every published parameter requires explicitly accepted evidence.",
        )
    try:
        revision.status = "published"
        revision.published_at = now()
        db.flush()

        payload = build_execution_component_payload(revision, db)
        package = finalize_execution_component(payload)
        revision.content_hash = package["content_hash"]
        db.flush()

        persisted_payload = build_execution_component_payload(revision, db)
        finalize_execution_component(
            persisted_payload, expected_hash=revision.content_hash
        )
        job.status = "published"
        db.commit()
    except ExecutionPackageError as error:
        db.rollback()
        raise api_error(409, error.code, str(error)) from error
    except ValidationError as error:
        db.rollback()
        raise execution_package_validation_error(error) from error
    return revision_detail(revision, db)


@router.get("/component-revisions/{revision_id}/execution-package")
def get_execution_package(revision_id: str, db: Session = Depends(get_db)):
    revision = db.get(ComponentRevision, revision_id)
    if revision is None:
        raise HTTPException(404, "revision not found")
    if revision.status != "published":
        raise api_error(
            409,
            "revision_not_published",
            "Only published component revisions have execution packages.",
        )
    if revision.content_hash is None:
        raise api_error(
            409,
            "execution_package_hash_mismatch",
            "The published revision has no content hash.",
        )
    try:
        payload = build_execution_component_payload(revision, db)
        package = finalize_execution_component(
            payload, expected_hash=revision.content_hash
        )
    except ExecutionPackageError as error:
        raise api_error(409, error.code, str(error)) from error
    except ValidationError as error:
        raise execution_package_validation_error(error) from error
    return Response(content=canonical_json_bytes(package), media_type="application/json")
