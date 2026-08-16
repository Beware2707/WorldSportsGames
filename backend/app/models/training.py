"""Training sessions: the only path that raises a career attribute.

Attributes raise the ceiling a race is validated against, so the server owns
every increment. A session is stored append-only whether or not it earned
anything — a rejected session is evidence, and silent rejection is
indistinguishable from a bug (the same rule the result audit trail follows).
"""

from datetime import datetime

from sqlalchemy import (
    DateTime,
    Float,
    ForeignKey,
    Index,
    String,
    func,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.career import CareerAthlete


class TrainingSession(Base):
    """One completed drill. Append-only."""

    __tablename__ = "training_session"
    __table_args__ = (
        Index("ix_training_athlete_time", "career_athlete_id", "created_at"),
        # One row per client submission: a replayed session must be answered
        # with the stored outcome, never trained twice.
        Index(
            "uq_training_client_ref",
            "career_athlete_id",
            "client_ref",
            unique=True,
            postgresql_where=text("client_ref IS NOT NULL"),
            sqlite_where=text("client_ref IS NOT NULL"),
        ),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    career_athlete_id: Mapped[int] = mapped_column(
        ForeignKey("career_athlete.id"), index=True
    )
    drill: Mapped[str] = mapped_column(String(48))
    attribute_key: Mapped[str] = mapped_column(String(32))
    # The raw performance the client reported, kept for audit even when the
    # session earned nothing.
    metric: Mapped[float] = mapped_column(Float)
    # 0..1 quality the server derived from the metric.
    quality: Mapped[float] = mapped_column(Float, default=0.0)
    attribute_before: Mapped[float] = mapped_column(Float, default=0.0)
    attribute_gain: Mapped[float] = mapped_column(Float, default=0.0)
    xp_awarded: Mapped[int] = mapped_column(default=0)
    accepted: Mapped[bool] = mapped_column(default=True, index=True)
    rejection_reason: Mapped[str | None] = mapped_column(String(200))
    client_ref: Mapped[str | None] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    athlete: Mapped[CareerAthlete] = relationship()
