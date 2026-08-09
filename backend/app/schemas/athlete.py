from datetime import date

from pydantic import BaseModel, ConfigDict

from app.schemas.catalog import CountryOut, DisciplineOut


class AthleteOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    slug: str
    given_name: str
    family_name: str
    full_name: str
    headshot_url: str | None
    is_active: bool
    country: CountryOut | None


class AthleteDetailOut(AthleteOut):
    date_of_birth: date | None
    sex: str | None
    bio: str | None
    disciplines: list[DisciplineOut]
