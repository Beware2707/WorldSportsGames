from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field

from app.schemas.catalog import CountryOut


class CareerAthleteIn(BaseModel):
    name: str = Field(min_length=1, max_length=96)
    gender: str = Field(pattern="^[MFX]$")
    country_id: int | None = None
    appearance: dict[str, Any] = Field(default_factory=dict)


class CareerAthleteOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    name: str
    gender: str
    career_stage: str
    total_xp: int
    country: CountryOut | None
    appearance: dict[str, Any]
    attributes: dict[str, float]


class ResultIn(BaseModel):
    event: str
    value_num: float
    reaction_ms: float | None = None
    splits: list[float] | None = Field(default=None, max_length=64)
    wind: float | None = Field(default=None, ge=-20, le=20)
    rng_seed: str | None = Field(default=None, max_length=64)
    input_digest: str | None = Field(default=None, max_length=128)


class ResultOut(BaseModel):
    """The server's authoritative answer to a submitted result.

    ``accepted`` is explicit: a rejected result is stored for audit but earns
    nothing, and the client is told exactly why.
    """

    accepted: bool
    rejection_reason: str | None
    event: str
    value_text: str
    is_personal_best: bool
    xp_awarded: int
    total_xp: int
    career_stage: str


class CareerLeaderboardRowOut(BaseModel):
    rank: int
    athlete_name: str
    country: CountryOut | None
    value_num: float
    value_text: str


class CareerLeaderboardOut(BaseModel):
    event: str
    scope: str
    period: str
    lower_is_better: bool
    unit: str
    rows: list[CareerLeaderboardRowOut]


class SaveOut(BaseModel):
    payload: dict[str, Any]
    version: int
    updated_at: datetime | None


class SaveIn(BaseModel):
    payload: dict[str, Any]
    # The version this write is based on. A mismatch is a conflict, answered
    # with 409 + the current server state so the client can merge.
    base_version: int


class EventCatalogueOut(BaseModel):
    code: str
    name: str
    value_kind: str
    lower_is_better: bool
    unit: str
    splits_expected: int
    requires_reaction: bool
