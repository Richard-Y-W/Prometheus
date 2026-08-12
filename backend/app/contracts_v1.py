from typing import Literal

from pydantic import BaseModel, Field, model_validator


class ResearchCreate(BaseModel):
    manufacturer: str = Field(min_length=1)
    part_number: str = Field(min_length=1)
    source_url: str | None = None


class ReviewDecision(BaseModel):
    field_name: str = Field(min_length=1)
    status: Literal["accepted", "rejected", "ambiguous"]
    note: str | None = None

    @model_validator(mode="after")
    def require_note_for_nonacceptance(self):
        if self.note is not None:
            self.note = self.note.strip() or None
        if self.status in {"rejected", "ambiguous"} and self.note is None:
            raise ValueError("rejected and ambiguous decisions require a note")
        return self


class ReviewRequest(BaseModel):
    reviewed_by: str = Field(min_length=1)
    decisions: list[ReviewDecision]
