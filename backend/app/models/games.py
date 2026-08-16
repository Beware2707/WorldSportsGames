"""Sports mini-games: catalogue, sessions, progression and achievements.

The engine is deliberately generic. A game is a row, not a class: its
``engine`` names a client-side mechanic (reaction, accuracy, timing, sequence)
and ``config`` carries the mechanic's parameters. Adding a game is a seed row
plus a client renderer for its mechanic — no schema change, no new endpoint.
"""

from datetime import date, datetime

from sqlalchemy import (
    JSON,
    Date,
    DateTime,
    ForeignKey,
    Index,
    String,
    UniqueConstraint,
    func,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.catalog import Sport

# Client-side mechanics. A new mechanic needs a renderer; a new *game* does not.
GAME_ENGINES = ("reaction", "accuracy", "timing", "sequence")

# How a raw score is interpreted — never assume higher is better.
SCORE_DIRECTIONS = ("higher_better", "lower_better")

ACHIEVEMENT_TRIGGERS = ("total_xp", "game_sessions", "game_score", "streak_days")


class Game(Base):
    __tablename__ = "game"

    id: Mapped[int] = mapped_column(primary_key=True)
    code: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(128))
    tagline: Mapped[str | None] = mapped_column(String(200))
    engine: Mapped[str] = mapped_column(String(16))
    sport_id: Mapped[int | None] = mapped_column(ForeignKey("sport.id"), index=True)
    # e.g. reaction: {"rounds": 5}, accuracy: {"targets": 10, "radius": 40}
    config: Mapped[dict] = mapped_column(JSON, default=dict)
    score_direction: Mapped[str] = mapped_column(String(16), default="higher_better")
    score_unit: Mapped[str | None] = mapped_column(String(16))
    is_active: Mapped[bool] = mapped_column(default=True)
    sort_order: Mapped[int] = mapped_column(default=0)

    sport: Mapped[Sport | None] = relationship()


class GameSession(Base):
    """One completed play. Append-only — a personal best is derived, never
    overwritten, so history survives."""

    __tablename__ = "game_session"
    __table_args__ = (
        Index("ix_game_session_leaderboard", "game_id", "score"),
        Index("ix_game_session_user", "user_id", "created_at"),
        # Replaying a captured submission is the cheapest cheat there is: it
        # needs no modified client, just the same request sent twice. One row
        # per client submission makes the replay answerable instead of
        # doubly rewarded.
        Index(
            "uq_game_session_client_ref",
            "user_id",
            "game_id",
            "client_ref",
            unique=True,
            postgresql_where=text("client_ref IS NOT NULL"),
            sqlite_where=text("client_ref IS NOT NULL"),
        ),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    game_id: Mapped[int] = mapped_column(ForeignKey("game.id"), index=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    score: Mapped[float] = mapped_column()
    xp_awarded: Mapped[int] = mapped_column(default=0)
    detail: Mapped[dict] = mapped_column(JSON, default=dict)
    client_ref: Mapped[str | None] = mapped_column(String(64))
    was_personal_best: Mapped[bool] = mapped_column(default=False)
    played_on: Mapped[date] = mapped_column(Date, index=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    game: Mapped[Game] = relationship()


class UserProgress(Base):
    """Aggregate progression. Denormalized from sessions for cheap reads, and
    recomputable from them if it ever drifts."""

    __tablename__ = "user_progress"

    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), primary_key=True)
    total_xp: Mapped[int] = mapped_column(default=0)
    sessions_played: Mapped[int] = mapped_column(default=0)
    last_played_on: Mapped[date | None] = mapped_column(Date)
    streak_days: Mapped[int] = mapped_column(default=0)


class Achievement(Base):
    __tablename__ = "achievement"

    id: Mapped[int] = mapped_column(primary_key=True)
    code: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(128))
    description: Mapped[str] = mapped_column(String(256))
    trigger: Mapped[str] = mapped_column(String(24))
    threshold: Mapped[float] = mapped_column()
    game_id: Mapped[int | None] = mapped_column(ForeignKey("game.id"))

    game: Mapped[Game | None] = relationship()


class UserAchievement(Base):
    __tablename__ = "user_achievement"
    __table_args__ = (
        UniqueConstraint("user_id", "achievement_id", name="uq_user_achievement"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    achievement_id: Mapped[int] = mapped_column(ForeignKey("achievement.id"))
    unlocked_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    achievement: Mapped[Achievement] = relationship()
