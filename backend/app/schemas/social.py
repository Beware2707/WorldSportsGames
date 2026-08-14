from datetime import datetime
from typing import Any

from pydantic import BaseModel, Field


class FriendCodeOut(BaseModel):
    code: str


class FriendRequestIn(BaseModel):
    code: str = Field(min_length=4, max_length=12)


class FriendOut(BaseModel):
    id: int
    display_name: str
    status: str  # pending | accepted
    direction: str  # incoming | outgoing


class AnalyticsEventIn(BaseModel):
    name: str = Field(min_length=1, max_length=48)
    properties: dict[str, Any] = Field(default_factory=dict)
    client_ts: datetime | None = None


class AnalyticsBatchIn(BaseModel):
    # A batch, not a stream: one HTTP call per event would be its own DoS.
    events: list[AnalyticsEventIn] = Field(min_length=1, max_length=100)


class AnalyticsRejection(BaseModel):
    index: int
    reason: str


class AnalyticsBatchOut(BaseModel):
    accepted: int
    rejected: list[AnalyticsRejection]
