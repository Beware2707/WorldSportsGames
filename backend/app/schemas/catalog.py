from pydantic import BaseModel, ConfigDict


class DisciplineOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    code: str
    name: str


class SportOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    code: str
    name: str
    category: str
    icon: str | None


class SportDetailOut(SportOut):
    disciplines: list[DisciplineOut]


class CountryOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    iso3: str
    iso2: str
    name: str
    flag_emoji: str | None
