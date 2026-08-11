import hashlib,json
from datetime import datetime,timezone
from fastapi import APIRouter,Depends,Header,HTTPException,Query
from pydantic import BaseModel,Field
from sqlalchemy import select,or_
from sqlalchemy.orm import Session
from .database import get_db
from .models_v1 import Manufacturer,Component,ComponentRevision,ComponentParameter,SourceDocument,EvidenceRecord,ResearchJob,ResearchJobEvent
from .research import mock_research,UNITS

router=APIRouter(prefix="/v1")
def now():return datetime.now(timezone.utc).isoformat()
def normalize(value:str):return "".join(c.lower() for c in value if c.isalnum())
class ResearchCreate(BaseModel):manufacturer:str=Field(min_length=1);part_number:str=Field(min_length=1);source_url:str|None=None
class ReviewDecision(BaseModel):field_name:str;status:str=Field(pattern="^(accepted|rejected)$")
class ReviewRequest(BaseModel):reviewed_by:str=Field(min_length=1);decisions:list[ReviewDecision]

def revision_detail(revision:ComponentRevision,db:Session):
    component=db.get(Component,revision.component_id);manufacturer=db.get(Manufacturer,component.manufacturer_id);parameters=[]
    for p in db.scalars(select(ComponentParameter).where(ComponentParameter.revision_id==revision.id)).all():
        evidence=db.scalars(select(EvidenceRecord).where(EvidenceRecord.parameter_id==p.id)).all();parameters.append({"id":p.id,"field_name":p.field_name,"value_si":p.value_si,"unit_si":p.unit_si,"original_value":p.original_value,"original_unit":p.original_unit,"evidence":[{c.name:getattr(e,c.name) for c in e.__table__.columns} for e in evidence]})
    return {"id":revision.id,"component_id":component.id,"manufacturer":manufacturer.name,"part_number":component.part_number,"model_class":component.model_class,"revision":revision.revision,"status":revision.status,"certification_tier":revision.certification_tier,"published_at":revision.published_at,"content_hash":revision.content_hash,"parameters":parameters}

@router.get("/components/search")
def search_components(q:str="",page:int=Query(1,ge=1),page_size:int=Query(20,ge=1,le=100),db:Session=Depends(get_db)):
    statement=select(Component);token=normalize(q)
    if token:statement=statement.where(or_(Component.normalized_part_number.contains(token),Component.family.ilike(f"%{q}%")))
    rows=db.scalars(statement.offset((page-1)*page_size).limit(page_size+1)).all();items=[]
    for c in rows[:page_size]:items.append({"id":c.id,"manufacturer":db.get(Manufacturer,c.manufacturer_id).name,"part_number":c.part_number,"family":c.family,"model_class":c.model_class})
    return {"items":items,"page":page,"page_size":page_size,"has_more":len(rows)>page_size}
@router.get("/components/{component_id}")
def component_detail(component_id:str,db:Session=Depends(get_db)):
    c=db.get(Component,component_id)
    if not c:raise HTTPException(404,"component not found")
    revisions=db.scalars(select(ComponentRevision).where(ComponentRevision.component_id==c.id)).all();return {"id":c.id,"manufacturer":db.get(Manufacturer,c.manufacturer_id).name,"part_number":c.part_number,"revisions":[revision_detail(r,db) for r in revisions]}
@router.get("/components/{component_id}/revisions/{revision_id}")
def get_revision(component_id:str,revision_id:str,db:Session=Depends(get_db)):
    r=db.get(ComponentRevision,revision_id)
    if not r or r.component_id!=component_id:raise HTTPException(404,"revision not found")
    return revision_detail(r,db)
@router.post("/research-jobs",status_code=202)
def create_research_job(body:ResearchCreate,idempotency_key:str=Header(alias="Idempotency-Key"),db:Session=Depends(get_db)):
    existing=db.scalar(select(ResearchJob).where(ResearchJob.idempotency_key==idempotency_key))
    if existing:return job_detail(existing,db)
    manufacturer=db.scalar(select(Manufacturer).where(Manufacturer.normalized_name==normalize(body.manufacturer)))
    if not manufacturer:manufacturer=Manufacturer(name=body.manufacturer,normalized_name=normalize(body.manufacturer));db.add(manufacturer);db.flush()
    component=db.scalar(select(Component).where(Component.manufacturer_id==manufacturer.id,Component.part_number==body.part_number))
    if not component:component=Component(manufacturer_id=manufacturer.id,part_number=body.part_number,normalized_part_number=normalize(body.part_number),family="fixture gearmotor",model_class="gearmotor");db.add(component);db.flush()
    cached_revision=db.scalar(select(ComponentRevision).where(ComponentRevision.component_id==component.id,ComponentRevision.revision=="fixture-1"))
    if cached_revision:
        status="published" if cached_revision.status=="published" else "ready_for_review"
        job=ResearchJob(idempotency_key=idempotency_key,status=status,query=body.model_dump_json(),revision_id=cached_revision.id);db.add(job);db.flush()
        for sequence,(stage,message) in enumerate([("cache_lookup","Reusable component revision found"),(status.replace("published","ready_for_review"),"Cached revision ready")],1):
            db.add(ResearchJobEvent(job_id=job.id,stage=stage,sequence=str(sequence),message=message))
        db.commit();return job_detail(job,db)
    extracted=mock_research(body.manufacturer,body.part_number);content=json.dumps(extracted["parameters"],sort_keys=True);revision=ComponentRevision(component_id=component.id,revision="fixture-1",certification_tier="provisional",status="draft",content_hash=hashlib.sha256(content.encode()).hexdigest());db.add(revision);db.flush()
    document=SourceDocument(source_url=body.source_url or "fixture://manufacturer/PM-36-GM",title="PM-36-GM Technical Data",document_hash=hashlib.sha256(b"PM-36-GM Technical Data fixture-1").hexdigest(),rights_status="fixture_redistributable",content_type="application/pdf");existing_doc=db.scalar(select(SourceDocument).where(SourceDocument.document_hash==document.document_hash));document=existing_doc or document;if_not_existing=existing_doc is None
    if if_not_existing:db.add(document);db.flush()
    for claim in extracted["claims"]:
        parameter=ComponentParameter(revision_id=revision.id,field_name=claim["field_name"],value_si=claim["normalized_value"],unit_si=claim["unit"],original_value=claim["original_value"],original_unit=claim["unit"]);db.add(parameter);db.flush();db.add(EvidenceRecord(parameter_id=parameter.id,source_document_id=document.id,source_locator=claim["page_or_figure"],source_excerpt=f"{claim['field_name']}: {claim['original_value']} {claim['unit']}",evidence_class="manufacturer_stated",confidence="0.99",extraction_method="fixture_table_parser_v1",review_status="pending"))
    job=ResearchJob(idempotency_key=idempotency_key,status="ready_for_review",query=body.model_dump_json(),revision_id=revision.id);db.add(job);db.flush()
    for sequence,stage in enumerate(["identity_resolution","source_acquisition","extraction","unit_validation","conflict_detection","model_compilation","missing_field_analysis","ready_for_review"],1):db.add(ResearchJobEvent(job_id=job.id,stage=stage,sequence=str(sequence),message=stage.replace("_"," ").title()))
    db.commit();return job_detail(job,db)
def job_detail(job,db):return {"id":job.id,"status":job.status,"revision_id":job.revision_id,"provider":job.provider,"events":[{c.name:getattr(e,c.name) for c in e.__table__.columns} for e in db.scalars(select(ResearchJobEvent).where(ResearchJobEvent.job_id==job.id).order_by(ResearchJobEvent.sequence)).all()],"candidate":revision_detail(db.get(ComponentRevision,job.revision_id),db) if job.revision_id else None}
@router.get("/research-jobs/{job_id}")
def get_job(job_id:str,db:Session=Depends(get_db)):
    j=db.get(ResearchJob,job_id)
    if not j:raise HTTPException(404,"research job not found")
    return job_detail(j,db)
@router.get("/research-jobs/{job_id}/events")
def get_events(job_id:str,db:Session=Depends(get_db)):return get_job(job_id,db)["events"]
@router.post("/research-jobs/{job_id}/review")
def review(job_id:str,body:ReviewRequest,db:Session=Depends(get_db)):
    job=db.get(ResearchJob,job_id)
    if not job:raise HTTPException(404,"research job not found")
    if db.get(ComponentRevision,job.revision_id).status=="published":raise HTTPException(409,"published revisions are immutable")
    parameters={p.field_name:p for p in db.scalars(select(ComponentParameter).where(ComponentParameter.revision_id==job.revision_id)).all()}
    for decision in body.decisions:
        if decision.field_name not in parameters:raise HTTPException(422,f"unknown field {decision.field_name}")
        for evidence in db.scalars(select(EvidenceRecord).where(EvidenceRecord.parameter_id==parameters[decision.field_name].id)).all():evidence.review_status=decision.status;evidence.reviewed_by=body.reviewed_by;evidence.reviewed_at=now()
    job.status="reviewed";db.commit();return job_detail(job,db)
@router.post("/research-jobs/{job_id}/publish")
def publish(job_id:str,idempotency_key:str=Header(alias="Idempotency-Key"),db:Session=Depends(get_db)):
    job=db.get(ResearchJob,job_id)
    if not job:raise HTTPException(404,"research job not found")
    revision=db.get(ComponentRevision,job.revision_id)
    if revision.status=="published":return revision_detail(revision,db)
    evidence=db.scalars(select(EvidenceRecord).join(ComponentParameter).where(ComponentParameter.revision_id==revision.id)).all()
    if not evidence or any(e.review_status!="accepted" for e in evidence):raise HTTPException(409,"all published parameters require accepted evidence")
    revision.status="published";revision.published_at=now();job.status="published";db.commit();return revision_detail(revision,db)
