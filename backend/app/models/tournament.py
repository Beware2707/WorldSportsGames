"""Career tournaments.

The server owns advancement. When a round starts, the server generates the AI
field (deterministic from the tournament seed) and stores it; when the player
submits a run, their *validated* time is compared against that stored field.
A client can render the opponents however it likes — it cannot invent its own
placing, because the field it would need to beat never leaves the server's
control.
"""

from datetime import datetime

from sqlalchemy import JSON, DateTime, ForeignKey, Index, String, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.career import CareerAthlete, GameResult

TOURNAMENT_ROUNDS = ("qualification", "heat", "semifinal", "final")
TOURNAMENT_STATUSES = ("in_progress", "eliminated", "completed")


class CareerTournament(Base):
    __tablename__ = "career_tournament"
    __table_args__ = (
        Index("ix_career_tournament_athlete", "career_athlete_id", "status"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    career_athlete_id: Mapped[int] = mapped_column(
        ForeignKey("career_athlete.id"), index=True
    )
    event_code: Mapped[str] = mapped_column(String(64))
    current_round: Mapped[str] = mapped_column(String(16), default="qualification")
    status: Mapped[str] = mapped_column(String(16), default="in_progress")
    # Final placing when completed; None until then. 1-3 is a medal.
    final_position: Mapped[int | None]
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    completed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    athlete: Mapped[CareerAthlete] = relationship()
    rounds: Mapped[list["TournamentRound"]] = relationship(
        back_populates="tournament", order_by="TournamentRound.id"
    )


class TournamentRound(Base):
    """One round with its server-generated AI field.

    ``field`` is [{name, iso3, time}] — fictional opponents with target times.
    The client uses it to drive AI pacing and presentation; the server uses it
    to compute the player's position. Both read the same stored truth.
    """

    __tablename__ = "tournament_round"

    id: Mapped[int] = mapped_column(primary_key=True)
    tournament_id: Mapped[int] = mapped_column(
        ForeignKey("career_tournament.id"), index=True
    )
    round_name: Mapped[str] = mapped_column(String(16))
    field: Mapped[list] = mapped_column(JSON, default=list)
    player_result_id: Mapped[int | None] = mapped_column(ForeignKey("game_result.id"))
    position: Mapped[int | None]
    advanced: Mapped[bool | None]
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    tournament: Mapped[CareerTournament] = relationship(back_populates="rounds")
    player_result: Mapped[GameResult | None] = relationship()
