from datetime import date
from typing import Any

from pydantic import BaseModel, ConfigDict, Field

from app.schemas.catalog import SportOut


class GameOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    code: str
    name: str
    tagline: str | None
    engine: str
    config: dict[str, Any]
    score_direction: str
    score_unit: str | None
    sport: SportOut | None
    personal_best: float | None = None


class SubmitScoreIn(BaseModel):
    """A completed play.

    ``score`` is validated against the game's own bounds server-side; the
    client cannot declare its own XP.

    ``client_ref`` makes resubmission safe. Replaying a captured request is
    the cheapest cheat available — it needs no modified client — so the same
    ref is answered with the original outcome rather than rewarded again.
    """

    score: float
    detail: dict[str, Any] = Field(default_factory=dict)
    client_ref: str | None = Field(default=None, min_length=1, max_length=64)


class AchievementOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    code: str
    name: str
    description: str


class SubmitScoreOut(BaseModel):
    score: float
    xp_awarded: int
    is_personal_best: bool
    total_xp: int
    level: int
    xp_into_level: int
    xp_for_next_level: int
    streak_days: int
    unlocked: list[AchievementOut]


class LeaderboardRowOut(BaseModel):
    rank: int
    user_id: int
    display_name: str
    score: float


class LeaderboardOut(BaseModel):
    game_code: str
    scope: str  # global | country | friends
    scope_label: str | None
    score_direction: str
    score_unit: str | None
    rows: list[LeaderboardRowOut]


class ProgressOut(BaseModel):
    total_xp: int
    level: int
    xp_into_level: int
    xp_for_next_level: int
    sessions_played: int
    streak_days: int
    last_played_on: date | None
    achievements: list[AchievementOut]
    locked_achievements: list[AchievementOut]


class DailyChallengeOut(BaseModel):
    """A rotating game-of-the-day.

    Chosen deterministically from the date so every player gets the same
    challenge without storing a schedule.
    """

    date: date
    game: GameOut
