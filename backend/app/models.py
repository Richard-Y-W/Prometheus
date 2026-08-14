from datetime import datetime, timezone
from uuid import uuid4
from sqlalchemy import String, Text, ForeignKey
from sqlalchemy.orm import Mapped, mapped_column
from .database import Base

def uid(): return str(uuid4())
def now(): return datetime.now(timezone.utc).isoformat()

class Record(Base):
    __abstract__ = True
    id: Mapped[str] = mapped_column(String, primary_key=True, default=uid)
    created_at: Mapped[str] = mapped_column(String, default=now)
    updated_at: Mapped[str] = mapped_column(String, default=now, onupdate=now)

class Project(Record):
    __tablename__="projects"; name: Mapped[str]=mapped_column(String); description: Mapped[str]=mapped_column(Text,default=""); unit_system: Mapped[str]=mapped_column(String,default="SI")
class Assembly(Record):
    __tablename__="assemblies"; project_id: Mapped[str]=mapped_column(ForeignKey("projects.id")); source_file: Mapped[str]=mapped_column(String); source_format: Mapped[str]=mapped_column(String); geometry_hash: Mapped[str]=mapped_column(String); hierarchy: Mapped[str]=mapped_column(Text)
class ComponentPackage(Record):
    __tablename__="component_packages"; manufacturer: Mapped[str]=mapped_column(String); part_number: Mapped[str]=mapped_column(String); revision: Mapped[str]=mapped_column(String,default="fixture-1"); model_level: Mapped[str]=mapped_column(String); model_class: Mapped[str]=mapped_column(String); validation_status: Mapped[str]=mapped_column(String,default="candidate"); version: Mapped[str]=mapped_column(String,default="1.0.0"); parameters: Mapped[str]=mapped_column(Text)
class EvidenceClaim(Record):
    __tablename__="evidence_claims"; component_package_id: Mapped[str]=mapped_column(ForeignKey("component_packages.id")); field_name: Mapped[str]=mapped_column(String); original_value: Mapped[str]=mapped_column(String); normalized_value: Mapped[str]=mapped_column(String); unit: Mapped[str]=mapped_column(String); source_url: Mapped[str]=mapped_column(String); source_document: Mapped[str]=mapped_column(String); page_or_figure: Mapped[str]=mapped_column(String); source_authority: Mapped[str]=mapped_column(String); extraction_status: Mapped[str]=mapped_column(String,default="validated")
class Connection(Record):
    __tablename__="connections"; project_id: Mapped[str]=mapped_column(ForeignKey("projects.id")); source_part: Mapped[str]=mapped_column(String); target_part: Mapped[str]=mapped_column(String); connection_type: Mapped[str]=mapped_column(String); definition: Mapped[str]=mapped_column(Text); confirmed_by_user: Mapped[str]=mapped_column(String,default="true")
class Scenario(Record):
    __tablename__="scenarios"; project_id: Mapped[str]=mapped_column(ForeignKey("projects.id")); name: Mapped[str]=mapped_column(String); natural_language_description: Mapped[str]=mapped_column(Text); structured_definition: Mapped[str]=mapped_column(Text); assumptions: Mapped[str]=mapped_column(Text)
class CheckRun(Record):
    __tablename__="check_runs"; scenario_id: Mapped[str]=mapped_column(ForeignKey("scenarios.id")); status: Mapped[str]=mapped_column(String); planned_checks: Mapped[str]=mapped_column(Text); omitted_checks: Mapped[str]=mapped_column(Text); manifest: Mapped[str]=mapped_column(Text)
class Finding(Record):
    __tablename__="findings"; check_run_id: Mapped[str]=mapped_column(ForeignKey("check_runs.id")); severity: Mapped[str]=mapped_column(String); failure_mechanism: Mapped[str]=mapped_column(String); affected_entities: Mapped[str]=mapped_column(Text); data: Mapped[str]=mapped_column(Text)
class Job(Record):
    __tablename__="jobs"; kind: Mapped[str]=mapped_column(String); status: Mapped[str]=mapped_column(String); progress: Mapped[str]=mapped_column(String); result_reference: Mapped[str]=mapped_column(String,default=""); error: Mapped[str]=mapped_column(Text,default="")
