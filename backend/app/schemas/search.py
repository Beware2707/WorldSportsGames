from pydantic import BaseModel

from app.schemas.athlete import AthleteOut
from app.schemas.catalog import CountryOut, SportOut
from app.schemas.competition import CompetitionOut


class SearchResults(BaseModel):
    query: str
    sports: list[SportOut]
    athletes: list[AthleteOut]
    countries: list[CountryOut]
    competitions: list[CompetitionOut]


class Suggestion(BaseModel):
    """One autocomplete row. ``kind`` tells the client where to navigate."""

    kind: str  # sport | athlete | competition | country
    id: int
    label: str
    sublabel: str | None = None
    slug: str | None = None


class Suggestions(BaseModel):
    query: str
    items: list[Suggestion]
