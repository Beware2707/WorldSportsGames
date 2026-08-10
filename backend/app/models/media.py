"""News, video and notifications.

Media is never scraped: rows carry the ``source`` and a canonical ``url`` back
to the publisher, and the platform stores a headline and thumbnail reference
only. Ingestion happens through provider adapters (see providers/base.py).
"""

from datetime import datetime

from sqlalchemy import (
    JSON,
    Column,
    DateTime,
    ForeignKey,
    Index,
    String,
    Table,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.athlete import Athlete
from app.models.catalog import Country, Sport

MEDIA_KINDS = ("article", "video", "photo")

NOTIFICATION_STATUSES = ("unread", "read")

# Media is tagged, not owned: one article can concern several sports/athletes.
media_sport = Table(
    "media_sport",
    Base.metadata,
    Column("media_id", ForeignKey("media_item.id"), primary_key=True),
    Column("sport_id", ForeignKey("sport.id"), primary_key=True),
)

media_athlete = Table(
    "media_athlete",
    Base.metadata,
    Column("media_id", ForeignKey("media_item.id"), primary_key=True),
    Column("athlete_id", ForeignKey("athlete.id"), primary_key=True),
)

media_country = Table(
    "media_country",
    Base.metadata,
    Column("media_id", ForeignKey("media_item.id"), primary_key=True),
    Column("country_id", ForeignKey("country.id"), primary_key=True),
)


class MediaItem(Base):
    __tablename__ = "media_item"
    __table_args__ = (
        UniqueConstraint("source", "external_id", name="uq_media_source_external"),
        Index("ix_media_kind_published", "kind", "published_at"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    kind: Mapped[str] = mapped_column(String(16), index=True)
    title: Mapped[str] = mapped_column(String(300))
    summary: Mapped[str | None] = mapped_column(String(1000))
    # Canonical link to the publisher — the platform links out, never rehosts.
    url: Mapped[str] = mapped_column(String(1000))
    thumbnail_url: Mapped[str | None] = mapped_column(String(1000))
    source: Mapped[str] = mapped_column(String(64), index=True)
    external_id: Mapped[str] = mapped_column(String(128))
    duration_seconds: Mapped[int | None]
    published_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)

    edition_id: Mapped[int | None] = mapped_column(
        ForeignKey("competition_edition.id"), index=True
    )

    sports: Mapped[list[Sport]] = relationship(secondary=media_sport)
    athletes: Mapped[list[Athlete]] = relationship(secondary=media_athlete)
    countries: Mapped[list[Country]] = relationship(secondary=media_country)


class Notification(Base):
    """A delivered notification.

    Rows are only created for kinds the recipient has enabled — the preference
    check happens at generation time, so a disabled kind leaves no trace.
    """

    __tablename__ = "notification"
    __table_args__ = (
        Index("ix_notification_user_created", "user_id", "created_at"),
        UniqueConstraint("user_id", "dedupe_key", name="uq_notification_dedupe"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("app_user.id"), index=True)
    kind: Mapped[str] = mapped_column(String(32), index=True)
    title: Mapped[str] = mapped_column(String(200))
    body: Mapped[str] = mapped_column(String(500))
    # Where tapping it should go, e.g. {"route": "/athletes/zellie-dunbar"}.
    payload: Mapped[dict] = mapped_column(JSON, default=dict)
    # Stable per (user, event) so re-running generation cannot double-notify.
    dedupe_key: Mapped[str] = mapped_column(String(128))
    read_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
