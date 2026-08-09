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
