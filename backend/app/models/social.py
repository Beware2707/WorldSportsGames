"""Friendships and analytics.

Friend discovery is by **friend code**, not email lookup: a code is shared
deliberately by its owner, so there is nothing to enumerate — the register
endpoint's 409 already leaks account existence and this must not add a second,
faster oracle.
"""

from datetime import datetime

from sqlalchemy import (
    JSON,
    DateTime,
    ForeignKey,
    Index,
    String,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.user import AppUser

FRIENDSHIP_STATUSES = ("pending", "accepted")

# The brief's §32 event list. Unknown names are rejected per-event so the
# table cannot fill with garbage, and adding a name here is a one-line change.
ANALYTICS_EVENT_NAMES = frozenset(
    {
        "GameStarted",
        "GameCompleted",
        "PersonalBest",
        "LevelUp",
        "AchievementUnlocked",
        "TournamentStarted",
        "TournamentCompleted",
        "Purchase",
        "SessionStarted",
        "SessionEnded",
        "Crash",
        "Performance",
    }
)


class FriendCode(Base):
    """Shareable invite code, generated on first request."""

    __tablename__ = "friend_code"

    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), primary_key=True)
    code: Mapped[str] = mapped_column(String(12), unique=True, index=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )


class Friendship(Base):
    """A friend request and, once accepted, a friendship.

    Stored once per pair (requester → addressee); acceptance makes it mutual.
    """

    __tablename__ = "friendship"
    __table_args__ = (
        UniqueConstraint("requester_id", "addressee_id", name="uq_friendship_pair"),
        Index("ix_friendship_addressee", "addressee_id", "status"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    requester_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    addressee_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"))
    status: Mapped[str] = mapped_column(String(12), default="pending")
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    responded_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    requester: Mapped[AppUser] = relationship(foreign_keys=[requester_id])
    addressee: Mapped[AppUser] = relationship(foreign_keys=[addressee_id])


class AnalyticsEvent(Base):
    """One client analytics event. Append-only; no personal data beyond the
    owning user id, and anonymous (pre-login) events are allowed."""

    __tablename__ = "analytics_event"
    __table_args__ = (Index("ix_analytics_name_received", "name", "received_at"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int | None] = mapped_column(ForeignKey("app_user.id"), index=True)
    name: Mapped[str] = mapped_column(String(48))
    properties: Mapped[dict] = mapped_column(JSON, default=dict)
    client_ts: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
