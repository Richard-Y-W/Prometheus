import hashlib, json
from contextlib import asynccontextmanager
from fastapi import FastAPI, Depends, HTTPException, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select
from sqlalchemy.orm import Session
from .database import Base, engine, get_db
from .models import Project, Assembly, ComponentPackage, EvidenceClaim, Connection, Scenario, CheckRun, Finding, Job
from .schemas import ProjectCreate, ConnectionCreate, ScenarioCreate
from .config import settings
from .api_v1 import router as v1_router
from . import models_v1

@asynccontextmanager
async def lifespan(_: FastAPI):
    Base.metadata.create_all(engine)
    yield

app=FastAPI(title="Prometheus API",version="0.1.0",lifespan=lifespan)
app.add_middleware(CORSMiddleware,allow_origins=["http://localhost:5173"],allow_methods=["*"],allow_headers=["*"])
app.include_router(v1_router)

def row(obj, *json_fields):
    value={c.name:getattr(obj,c.name) for c in obj.__table__.columns}
    for field in json_fields:
        if value.get(field): value[field]=json.loads(value[field])
    return value

@app.get("/health")
def health(): return {"status":"ok","provider":settings.llm_provider}

@app.get("/v1/health",tags=["service"])
def versioned_health():
    return {"status":"ok","api_version":"v1","service":"component-research","provider":settings.llm_provider}

@app.post("/projects",status_code=201)
def create_project(body: ProjectCreate, db: Session=Depends(get_db)):
    p=Project(**body.model_dump()); db.add(p); db.commit(); return row(p)

@app.get("/projects")
def list_projects(db: Session=Depends(get_db)): return [row(x) for x in db.scalars(select(Project).order_by(Project.updated_at.desc())).all()]

@app.get("/projects/{project_id}")
def get_project(project_id: str, db: Session=Depends(get_db)):
    p=db.get(Project,project_id)
    if not p: raise HTTPException(404,"project not found")
    assemblies=[row(x,"hierarchy") for x in db.scalars(select(Assembly).where(Assembly.project_id==project_id)).all()]
    return {**row(p),"assemblies":assemblies}

def fixture_assembly(project_id: str):
    hierarchy={"id":"assembly","name":"Motor-driven arm","type":"assembly","children":[
      {"id":"base","name":"Base plate","type":"part","badge":"Geometry Only","shape":"box","position":[0,0,0],"scale":[3.2,.3,2.2]},
      {"id":"motor","name":"Motor placeholder","type":"part","badge":"Geometry Only","shape":"cylinder","position":[0,.55,0],"scale":[.65,1,.65]},
      {"id":"arm","name":"Arm","type":"part","badge":"Geometry Only","shape":"box","position":[1.55,.55,0],"scale":[2.6,.24,.35]},
      {"id":"payload","name":"8 kg payload","type":"payload","badge":"Envelope Model","shape":"box","position":[3,.55,0],"scale":[.65,.8,.65]}]}
    return Assembly(project_id=project_id,source_file="motor-arm-fixture.step",source_format="STEP (fixture tessellation)",geometry_hash=hashlib.sha256(b"prometheus-motor-arm-v1").hexdigest(),hierarchy=json.dumps(hierarchy))

@app.post("/projects/{project_id}/cad-imports",status_code=202)
async def import_cad(project_id: str, fixture: bool=True, file: UploadFile|None=File(default=None), db: Session=Depends(get_db)):
    if not db.get(Project,project_id): raise HTTPException(404,"project not found")
    if not fixture:
        if not file or not file.filename.lower().endswith((".step",".stp")): raise HTTPException(415,"V1 accepts STEP files only")
        data=await file.read(settings.max_upload_mb*1024*1024+1)
        if len(data)>settings.max_upload_mb*1024*1024: raise HTTPException(413,"file too large")
        if not (data[:64].lstrip().startswith(b"ISO-10303-21") or b"ISO-10303-21" in data[:256]): raise HTTPException(422,"invalid STEP signature")
        raise HTTPException(501,"real STEP tessellation adapter is not installed; load the verified fixture")
    assembly=fixture_assembly(project_id); job=Job(kind="cad_import",status="completed",progress="Fixture hierarchy and tessellation ready")
    db.add_all([assembly,job]); db.commit(); job.result_reference=assembly.id; db.commit()
    return {**row(job),"assembly":row(assembly,"hierarchy"),"limitations":["Fixture tessellation; Open Cascade STEP adapter not installed","Neutral CAD mates are unavailable","Material and mass are not inferred from geometry"]}

@app.post("/component-research")
def research_component():
    raise HTTPException(
        status_code=410,
        detail={
            "code": "legacy_evidence_path_retired",
            "message": (
                "The legacy component evidence path is retired. Use versioned "
                "research and explicit evidence review through /v1/research-jobs."
            ),
        },
    )

def package_response(package,db):
    claims=db.scalars(select(EvidenceClaim).where(EvidenceClaim.component_package_id==package.id)).all()
    return {**row(package,"parameters"),"evidence":[row(c) for c in claims]}

@app.get("/component-packages/{package_id}")
def get_package(package_id: str,db: Session=Depends(get_db)):
    p=db.get(ComponentPackage,package_id)
    if not p: raise HTTPException(404,"component package not found")
    return package_response(p,db)

@app.post("/component-packages/{package_id}/confirm")
def confirm_package(package_id: str):
    del package_id
    raise HTTPException(
        status_code=410,
        detail={
            "code": "legacy_evidence_path_retired",
            "message": (
                "The legacy component confirmation path is retired. Use versioned "
                "research and explicit evidence review through /v1/research-jobs."
            ),
        },
    )

@app.post("/projects/{project_id}/connections",status_code=201)
def create_connection(project_id: str,body: ConnectionCreate,db: Session=Depends(get_db)):
    c=Connection(project_id=project_id,source_part=body.source_part,target_part=body.target_part,connection_type=body.connection_type,definition=json.dumps({"axis":body.axis,"limits_deg":body.limits_deg}))
    db.add(c); db.commit(); return row(c,"definition")

@app.post("/projects/{project_id}/scenarios",status_code=201)
def create_scenario(project_id: str,body: ScenarioCreate,db: Session=Depends(get_db)):
    s=Scenario(project_id=project_id,name=body.name,natural_language_description=body.natural_language_description,structured_definition=json.dumps(body.definition.model_dump()),assumptions=json.dumps(["Horizontal pose is worst-case for gravity torque","Symmetric triangular motion profile","Payload treated as point mass at arm end"]))
    db.add(s); db.commit(); return row(s,"structured_definition","assumptions")

@app.post("/scenarios/{scenario_id}/compile")
def compile_scenario(scenario_id: str):
    del scenario_id
    raise HTTPException(
        status_code=410,
        detail={
            "code": "legacy_analysis_path_retired",
            "message": (
                "The legacy Python planning path is retired. Planning will resume "
                "through the reviewed execution-package path."
            ),
        },
    )

@app.post("/scenarios/{scenario_id}/runs")
def run_checks(scenario_id: str):
    del scenario_id
    raise HTTPException(
        status_code=501,
        detail={
            "code": "authoritative_execution_unavailable",
            "message": (
                "Authoritative execution is unavailable. Reviewed execution-package-"
                "to-C++ analysis is Program 01B work; this endpoint cannot issue "
                "engineering findings."
            ),
        },
    )

def run_response(run,db):
    findings=db.scalars(select(Finding).where(Finding.check_run_id==run.id)).all()
    return {**row(run,"planned_checks","omitted_checks","manifest"),"findings":[row(f,"affected_entities","data") for f in findings]}

@app.get("/runs/{run_id}")
def get_run(run_id: str,db: Session=Depends(get_db)):
    r=db.get(CheckRun,run_id)
    if not r: raise HTTPException(404,"run not found")
    return run_response(r,db)

@app.get("/runs/{run_id}/findings")
def get_findings(run_id: str,db: Session=Depends(get_db)):
    return run_response(db.get(CheckRun,run_id),db)["findings"]

@app.get("/jobs/{job_id}")
def get_job(job_id: str,db: Session=Depends(get_db)):
    j=db.get(Job,job_id)
    if not j: raise HTTPException(404,"job not found")
    return row(j)
