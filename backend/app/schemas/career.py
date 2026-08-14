import json
from datetime import datetime
from typing import Annotated, Any

from pydantic import BaseModel, ConfigDict, Field, field_validator

from app.schemas.catalog import CountryOut

# A JSON float that rejects NaN / +-Infinity. Untyped floats accept them, and
# because NaN compares False to everything, a NaN reaction_ms slipped past the
# false-start floor AND the split-coherence sum — laundering a real false
# start into a valid, XP-earning result. It also crashed the stored-row
# formatter (int(nan) -> ValueError -> 500). Rejecting at the schema boundary
# closes both the crash and the bypass before the handler ever runs.
FiniteFloat = Annotated[float, Field(allow_inf_nan=False)]

# Client-controlled JSON blobs need a ceiling, exactly as analytics events do
# — an unbounded JSON column is a slow-motion disk leak.
_MAX_JSON_BYTES = 8192


def _bounded_json(value: dict[str, Any]) -> dict[str, Any]:
    if len(json.dumps(value, default=str)) > _MAX_JSON_BYTES:
        raise ValueError(f"payload exceeds {_MAX_JSON_BYTES} bytes")
    return value


class CareerAthleteIn(BaseModel):
    name: str = Field(min_length=1, max_length=96)
    gender: str = Field(pattern="^[MFX]$")
    country_id: int | None = None
    appearance: dict[str, Any] = Field(default_factory=dict)

    @field_validator("appearance")
    @classmethod
    def _bound_appearance(cls, value: dict[str, Any]) -> dict[str, Any]:
        return _bounded_json(value)


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
    event: str = Field(min_length=1, max_length=64)
    value_num: FiniteFloat
    reaction_ms: FiniteFloat | None = None
    splits: list[FiniteFloat] | None = Field(default=None, max_length=64)
    wind: FiniteFloat | None = Field(default=None, ge=-20, le=20)
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
    base_version: int = Field(ge=0)

    @field_validator("payload")
    @classmethod
    def _bound_payload(cls, value: dict[str, Any]) -> dict[str, Any]:
        return _bounded_json(value)


class EventCatalogueOut(BaseModel):
    code: str
    name: str
    value_kind: str
    lower_is_better: bool
    unit: str
    splits_expected: int
    requires_reaction: bool
