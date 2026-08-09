from datetime import date

from pydantic import BaseModel, ConfigDict

from app.schemas.catalog import CountryOut, SportOut


class EditionOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    label: str
    year: int
    start_date: date | None
    end_date: date | None
    host_city: str | None
    host_country: CountryOut | None
    status: str


class CompetitionOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    slug: str
    name: str
    level: str
    sport: SportOut | None


class CompetitionDetailOut(CompetitionOut):
    editions: list[EditionOut]
