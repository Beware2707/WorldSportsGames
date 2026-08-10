from datetime import date

from pydantic import BaseModel, ConfigDict

from app.schemas.catalog import CountryOut, DisciplineOut


class RankingEntryOut(BaseModel):
    """One rung of a ladder. ``entity`` is resolved by scope so the client
    renders athlete/team/country ladders with one component."""

    rank: int
    points: float | None
    entity_id: int
    entity_name: str | None
    entity_subtitle: str | None
    entity_slug: str | None


class RankingOut(BaseModel):
    scope: str
    methodology: str
    discipline: DisciplineOut | None
    as_of: date | None
    entries: list[RankingEntryOut]


class RecordOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    kind: str
    event_name: str
    gender: str
    value_text: str
    value_kind: str
    unit: str | None
    set_on: date | None
    location: str | None
    discipline: DisciplineOut
    country: CountryOut | None
    holder_name: str | None = None
    holder_slug: str | None = None


class MedalTallyOut(BaseModel):
    country: CountryOut
    gold: int
    silver: int
    bronze: int
    total: int
    rank: int


class MedalTableOut(BaseModel):
    edition_id: int | None
    edition_label: str | None
    rows: list[MedalTallyOut]


class CountryProfileOut(BaseModel):
    country: CountryOut
    athlete_count: int
    medals: MedalTallyOut | None
    athletes: list[dict]
    records: list[RecordOut]


class AthleteProfileOut(BaseModel):
    """Everything the athlete screen needs in one round-trip."""

    athlete: dict
    disciplines: list[DisciplineOut]
    personal_bests: list[RecordOut]
    medals: list[dict]
    recent_results: list[dict]
    world_rankings: list[dict]
