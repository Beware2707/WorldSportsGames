from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.schemas.athlete import AthleteOut
from app.schemas.catalog import DisciplineOut
from app.schemas.competition import EditionOut


class EventOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    name: str
    gender: str
    phase: str
    status: str
    scheduled_start: datetime | None
    discipline: DisciplineOut


class EventResultOut(BaseModel):
    """One athlete's line in an event: participation + normalized result."""

    athlete: AthleteOut
    bib: str | None
    lane: int | None
    position: int | None
    status: str | None  # None = no result recorded yet
    value_kind: str | None
    value_text: str | None
    qualified: bool | None


class EventDetailOut(EventOut):
    edition: EditionOut
    competition_name: str
    competition_slug: str
    results: list[EventResultOut]
