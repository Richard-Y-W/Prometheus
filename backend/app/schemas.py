from typing import Literal
from pydantic import BaseModel, Field, model_validator

class ProjectCreate(BaseModel):
    name: str = Field(min_length=1, max_length=120)
    description: str = ""
    unit_system: Literal["SI", "US"] = "SI"

class ResearchRequest(BaseModel):
    manufacturer: str = "Prometheus Fixture Works"
    part_number: str = "PM-36-GM"
    source_url: str | None = None

class ConnectionCreate(BaseModel):
    source_part: str
    target_part: str
    connection_type: Literal["fixed", "revolute", "sliding"] = "revolute"
    axis: list[float] = [0, 0, 1]
    limits_deg: list[float] = [0, 90]

class ScenarioDefinition(BaseModel):
    payload_kg: float = Field(gt=0)
    arm_length_m: float = Field(gt=0)
    rotation_deg: float = Field(gt=0, le=360)
    movement_s: float = Field(gt=0)
    hold_s: float = Field(ge=0)
    cycle_s: float = Field(gt=0)
    ambient_c: float = Field(ge=-40, le=150)
    safety_factor: float = Field(default=1.2, ge=1)
    @model_validator(mode="after")
    def cycle_contains_actions(self):
        if self.cycle_s < self.movement_s + self.hold_s: raise ValueError("cycle_s must include movement and hold")
        return self

class ScenarioCreate(BaseModel):
    name: str = "Motor arm duty cycle"
    natural_language_description: str
    definition: ScenarioDefinition
