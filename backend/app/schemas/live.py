from datetime import datetime
from typing import Any

from pydantic import BaseModel

from app.schemas.event import EventOut


class LiveEventOut(BaseModel):
    """A genuinely live event, as served by GET /api/v1/live and the WS snapshot."""

    event: EventOut
    edition_label: str
    competition_name: str
    competition_slug: str
    current_phase: str | None
    last_seq: int
    updated_at: datetime


class LiveUpdateMessage(BaseModel):
    """WS diff frame. ``kind`` ∈ status | progress | results | note."""

    type: str = "update"
    event_id: int
    seq: int
    kind: str
    payload: dict[str, Any]
