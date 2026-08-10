from datetime import datetime
from typing import Any

from sqlalchemy import JSON, Boolean, DateTime, ForeignKey, String, UniqueConstraint, func
from sqlalchemy.orm import Mapped, mapped_column

from app.db.base import Base

FAVORITE_ENTITY_TYPES = ("sport", "discipline", "athlete", "country", "competition")

# Notification kinds a user can opt in/out of. Adding a kind here is the only
# change needed — preferences default to enabled when no row exists.
NOTIFICATION_KINDS = (
    "athlete_event_start",
    "competition_start",
    "live_result",
    "medal_result",
    "record_broken",
    "event_reminder",
    "followed_athlete_result",
    "followed_country_event",
    "breaking_news",
)


class AppUser(Base):
    __tablename__ = "app_user"

    id: Mapped[int] = mapped_column(primary_key=True)
    email: Mapped[str] = mapped_column(String(320), unique=True, index=True)
    hashed_password: Mapped[str] = mapped_column(String(128))
    display_name: Mapped[str] = mapped_column(String(96))
    is_active: Mapped[bool] = mapped_column(default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )


class Favorite(Base):
    """A follow. Entity-polymorphic (type + id) so "follow anything" stays
    uniform; the service layer validates the target exists before inserting."""

    __tablename__ = "favorite"
    __table_args__ = (
        UniqueConstraint("user_id", "entity_type", "entity_id", name="uq_favorite"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    entity_type: Mapped[str] = mapped_column(String(16), index=True)
    entity_id: Mapped[int] = mapped_column()
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )


class UserPreference(Base):
    """Free-form per-user settings (onboarding state, feed configuration).

    JSON-valued so new preferences never require a migration.
    """

    __tablename__ = "user_preference"
    __table_args__ = (UniqueConstraint("user_id", "key", name="uq_user_preference"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    key: Mapped[str] = mapped_column(String(64))
    value: Mapped[Any] = mapped_column(JSON)


class NotificationPreference(Base):
    """Opt-out record for one notification kind. Absence means enabled."""

    __tablename__ = "notification_preference"
    __table_args__ = (
        UniqueConstraint("user_id", "kind", name="uq_notification_preference"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    kind: Mapped[str] = mapped_column(String(32))
    enabled: Mapped[bool] = mapped_column(Boolean, default=True)
