from datetime import datetime

from sqlalchemy import JSON, DateTime, ForeignKey, String, UniqueConstraint, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.event import Event

LIVE_STATUSES = ("live", "finished")
LIVE_UPDATE_KINDS = ("status", "progress", "results", "note")


class LiveEvent(Base):
    """Live-coverage record for an event. A row exists only while an event has
    genuine live coverage — its presence is what makes anything render as LIVE."""

    __tablename__ = "live_event"

    id: Mapped[int] = mapped_column(primary_key=True)
    event_id: Mapped[int] = mapped_column(ForeignKey("event.id"), unique=True, index=True)
    status: Mapped[str] = mapped_column(String(16), default="live", index=True)
    current_phase: Mapped[str | None] = mapped_column(String(64))
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    event: Mapped[Event] = relationship()
    updates: Mapped[list["LiveUpdate"]] = relationship(
        back_populates="live_event", order_by="LiveUpdate.seq"
    )


class LiveUpdate(Base):
    """Append-only stream of diffs for a live event; ``seq`` orders them."""

    __tablename__ = "live_update"
    __table_args__ = (UniqueConstraint("live_event_id", "seq", name="uq_live_update_seq"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    live_event_id: Mapped[int] = mapped_column(ForeignKey("live_event.id"), index=True)
    seq: Mapped[int] = mapped_column()
    kind: Mapped[str] = mapped_column(String(16))
    payload: Mapped[dict] = mapped_column(JSON)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    live_event: Mapped[LiveEvent] = relationship(back_populates="updates")
