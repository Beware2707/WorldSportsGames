"""Player-owned career athletes and 3D game results.

Deliberately separate from the real-world sports catalogue: ``Athlete`` is a
data record about a real competitor; ``CareerAthlete`` is a game character a
user owns. Sharing a table would let game progression corrupt the catalogue
and make "list athletes" return player avatars (see
docs/unreal/BACKEND_MIGRATION.md §2).
"""

from datetime import datetime

from sqlalchemy import (
    JSON,
    DateTime,
    Float,
    ForeignKey,
    Index,
    String,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.catalog import Country

CAREER_STAGES = (
    "rookie",
    "regional",
    "national",
    "elite",
    "world_class",
    "champion",
    "legend",
)

ATTRIBUTE_KEYS = (
    "reaction",
    "acceleration",
    "max_speed",
    "stride_efficiency",
    "stamina",
    "recovery",
    "technique",
)


class CareerAthlete(Base):
    __tablename__ = "career_athlete"
    # One career athlete per user in v1; relaxing this later is additive.
    __table_args__ = (UniqueConstraint("user_id", name="uq_career_athlete_user"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    name: Mapped[str] = mapped_column(String(96))
    gender: Mapped[str] = mapped_column(String(1))  # M / F / X
    country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"), index=True)
    # Modular character configuration (head, hair, skin, outfit …). JSON so
    # the Unreal character system can evolve without server migrations.
    appearance: Mapped[dict] = mapped_column(JSON, default=dict)
    career_stage: Mapped[str] = mapped_column(String(16), default="rookie")
    total_xp: Mapped[int] = mapped_column(default=0)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    country: Mapped[Country | None] = relationship()
    attributes: Mapped[list["CareerAttribute"]] = relationship(
        back_populates="athlete", cascade="all, delete-orphan"
    )


class CareerAttribute(Base):
    __tablename__ = "career_attribute"
    __table_args__ = (
        UniqueConstraint("career_athlete_id", "key", name="uq_career_attribute"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    career_athlete_id: Mapped[int] = mapped_column(
        ForeignKey("career_athlete.id"), index=True
    )
    key: Mapped[str] = mapped_column(String(24))
    # 0–100. Raises the ceiling; never wins the race by itself.
    value: Mapped[float] = mapped_column(Float, default=40.0)

    athlete: Mapped[CareerAthlete] = relationship(back_populates="attributes")


class GameResult(Base):
    """One completed 3D event, valid or not.

    Rejected results are stored with ``is_valid=False`` and a reason instead
    of being discarded: silent rejection is indistinguishable from a bug, and
    the audit trail is what makes cheat patterns visible. Only valid rows are
    eligible for leaderboards and personal bests.
    """

    __tablename__ = "game_result"
    __table_args__ = (
        Index("ix_game_result_board", "event_code", "is_valid", "value_num"),
        Index("ix_game_result_athlete", "career_athlete_id", "created_at"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    career_athlete_id: Mapped[int] = mapped_column(
        ForeignKey("career_athlete.id"), index=True
    )
    event_code: Mapped[str] = mapped_column(String(64))
    value_kind: Mapped[str] = mapped_column(String(16))  # time | distance | …
    value_num: Mapped[float] = mapped_column(Float)
    value_text: Mapped[str] = mapped_column(String(64))
    reaction_ms: Mapped[float | None] = mapped_column(Float)
    splits: Mapped[list | None] = mapped_column(JSON)
    wind: Mapped[float | None] = mapped_column(Float)
    # Determinism inputs, kept for deferred server-side re-simulation of
    # leaderboard-topping results.
    rng_seed: Mapped[str | None] = mapped_column(String(64))
    input_digest: Mapped[str | None] = mapped_column(String(128))
    is_valid: Mapped[bool] = mapped_column(default=True, index=True)
    rejection_reason: Mapped[str | None] = mapped_column(String(200))
    xp_awarded: Mapped[int] = mapped_column(default=0)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    athlete: Mapped[CareerAthlete] = relationship()


class CareerSave(Base):
    """Cloud save blob with optimistic concurrency.

    ``version`` must match on write; a mismatch returns the server state so
    the client can merge rather than silently losing a device's progress.
    """

    __tablename__ = "career_save"

    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), primary_key=True)
    payload: Mapped[dict] = mapped_column(JSON, default=dict)
    version: Mapped[int] = mapped_column(default=1)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )
