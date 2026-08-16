from typing import Annotated

from pydantic import BaseModel, Field

# Non-finite floats slide past every bounds check, because NaN compares
# False to everything. The result schema learned this the hard way.
FiniteFloat = Annotated[float, Field(allow_inf_nan=False)]


class DrillOut(BaseModel):
    code: str
    name: str
    attribute: str
    metric_unit: str
    lower_is_better: bool
    best_metric: float
    worst_metric: float


class TrainingIn(BaseModel):
    drill: str = Field(min_length=1, max_length=48)
    """What the athlete DID — never what they think they earned."""
    metric: FiniteFloat
    client_ref: str | None = Field(default=None, min_length=1, max_length=64)


class TrainingOut(BaseModel):
    """The server's authoritative answer to a training session."""

    accepted: bool
    rejection_reason: str | None
    drill: str
    attribute: str
    quality: float
    attribute_before: float
    attribute_after: float
    attribute_gain: float
    xp_awarded: int
    total_xp: int
    career_stage: str
    """Points still available for this attribute in the rolling day."""
    daily_remaining: float


class CareerRecordOut(BaseModel):
    event: str
    unit: str
    lower_is_better: bool
    personal_best: float | None
    personal_best_text: str | None
    season_best: float | None
    season_best_text: str | None
    world_best: float | None
    world_best_text: str | None
    world_best_holder: str | None


class CareerStatsOut(BaseModel):
    races: int
    valid_races: int
    rejected_races: int
    wins: int
    training_sessions: int
    best_reaction_ms: float | None
    average_reaction_ms: float | None
    total_distance_m: float
    total_xp: int
    career_stage: str
