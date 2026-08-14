"""Historical v1 metadata reads and explicitly retired trust-boundary routes."""

from __future__ import annotations

from typing import NoReturn

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import or_, select
from sqlalchemy.orm import Session

from .database import get_db
from .fixture_catalog import normalize_identity
from .models_v1 import (
    Component,
    ComponentParameter,
    ComponentRevision,
    EvidenceRecord,
    Manufacturer,
    ResearchJob,
    ResearchJobEvent,
)


router = APIRouter(prefix="/v1")
_RETIRED_RESPONSE = {
    410: {
        "description": (
            "The v1 review/publication trust boundary is retired; use /api/v2."
        )
    }
}


def retired_v1_trust_boundary() -> NoReturn:
    raise HTTPException(
        status_code=410,
        detail={
            "code": "v1_trust_boundary_retired",
            "message": "The v1 review/publication boundary is retired.",
            "migration_guide": "/docs/migration/program-01a-v1-to-v2.md",
        },
    )


def revision_detail(revision: ComponentRevision, db: Session) -> dict[str, object]:
    """Return labeled history without reconstructing execution-package bytes."""

    component = db.get(Component, revision.component_id)
    if component is None:
        raise HTTPException(409, "historical revision references a missing component")
    manufacturer = db.get(Manufacturer, component.manufacturer_id)
    if manufacturer is None:
        raise HTTPException(409, "historical component references a missing manufacturer")
    parameters = []
    statement = (
        select(ComponentParameter)
        .where(ComponentParameter.revision_id == revision.id)
        .order_by(ComponentParameter.field_name)
    )
    for parameter in db.scalars(statement).all():
        evidence = db.scalars(
            select(EvidenceRecord).where(
                EvidenceRecord.parameter_id == parameter.id
            )
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
                    {
                        column.name: getattr(record, column.name)
                        for column in record.__table__.columns
                    }
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
        "publication_integrity": revision.publication_integrity,
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
            "manufacturer": db.get(
                Manufacturer, component.manufacturer_id
            ).name,
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
    if component is None:
        raise HTTPException(404, "component not found")
    revisions = db.scalars(
        select(ComponentRevision).where(
            ComponentRevision.component_id == component.id
        )
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
    if revision is None or revision.component_id != component_id:
        raise HTTPException(404, "revision not found")
    return revision_detail(revision, db)


@router.post(
    "/research-jobs",
    status_code=410,
    response_model=None,
    responses=_RETIRED_RESPONSE,
)
def create_research_job() -> NoReturn:
    retired_v1_trust_boundary()


def job_detail(job: ResearchJob, db: Session) -> dict[str, object]:
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
            {
                column.name: getattr(event, column.name)
                for column in event.__table__.columns
            }
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
    if job is None:
        raise HTTPException(404, "research job not found")
    return job_detail(job, db)


@router.get("/research-jobs/{job_id}/events")
def get_events(job_id: str, db: Session = Depends(get_db)):
    return get_job(job_id, db)["events"]


@router.post(
    "/research-jobs/{job_id}/review",
    status_code=410,
    response_model=None,
    responses=_RETIRED_RESPONSE,
)
def review(job_id: str) -> NoReturn:
    del job_id
    retired_v1_trust_boundary()


@router.post(
    "/research-jobs/{job_id}/publish",
    status_code=410,
    response_model=None,
    responses=_RETIRED_RESPONSE,
)
def publish(job_id: str) -> NoReturn:
    del job_id
    retired_v1_trust_boundary()


@router.get(
    "/component-revisions/{revision_id}/execution-package",
    status_code=410,
    response_model=None,
    responses=_RETIRED_RESPONSE,
)
def get_execution_package(revision_id: str) -> NoReturn:
    del revision_id
    retired_v1_trust_boundary()


__all__ = ["retired_v1_trust_boundary", "revision_detail", "router"]
