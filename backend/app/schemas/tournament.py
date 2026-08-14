from typing import Any

from pydantic import BaseModel


class TournamentStartIn(BaseModel):
    event: str


class RoundOut(BaseModel):
    round: str
    # [{name, iso3, time}] — the server-generated AI field. Presentation data
    # for the client AND the server's own scoring truth.
    field: list[dict[str, Any]]
    position: int | None
    advanced: bool | None


class TournamentOut(BaseModel):
    id: int
    event: str
    current_round: str
    status: str
    final_position: int | None
    rounds: list[RoundOut]


class TournamentResultOut(BaseModel):
    accepted: bool
    rejection_reason: str | None
    round: str
    position: int | None
    advanced: bool | None
    tournament_status: str
    final_position: int | None
    is_personal_best: bool
    xp_awarded: int
    total_xp: int
