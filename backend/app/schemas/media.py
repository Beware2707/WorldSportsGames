from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict


class MediaItemOut(BaseModel):
    """A media item. ``url`` always points back to the publisher — the
    platform links out and never rehosts third-party content."""

    id: int
    kind: str
    title: str
    summary: str | None
    url: str
    thumbnail_url: str | None
    source: str
    duration_seconds: int | None
    published_at: datetime
    sports: list[str]
    athletes: list[str]
    countries: list[str]

    @classmethod
    def from_model(cls, item) -> "MediaItemOut":
        return cls(
            id=item.id,
            kind=item.kind,
            title=item.title,
            summary=item.summary,
            url=item.url,
            thumbnail_url=item.thumbnail_url,
            source=item.source,
            duration_seconds=item.duration_seconds,
            published_at=item.published_at,
            sports=[s.name for s in item.sports],
            athletes=[a.full_name for a in item.athletes],
            countries=[c.iso3 for c in item.countries],
        )


class NotificationOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    kind: str
    title: str
    body: str
    payload: dict[str, Any]
    read_at: datetime | None
    created_at: datetime


class UnreadCountOut(BaseModel):
    unread: int
