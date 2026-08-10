from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

from app.models.user import FAVORITE_ENTITY_TYPES, NOTIFICATION_KINDS

EntityType = Literal[FAVORITE_ENTITY_TYPES]  # type: ignore[valid-type]
NotificationKind = Literal[NOTIFICATION_KINDS]  # type: ignore[valid-type]


class FavoriteIn(BaseModel):
    entity_type: EntityType
    entity_id: int


class FavoriteOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    entity_type: str
    entity_id: int
    # Resolved display fields so the client can render a follow list without
    # N round-trips. None when the target has since been removed.
    name: str | None = None
    subtitle: str | None = None
    slug: str | None = None


class FavoritesBulkIn(BaseModel):
    """Onboarding submits the whole selection at once."""

    favorites: list[FavoriteIn] = Field(default_factory=list, max_length=200)


class NotificationPreferenceOut(BaseModel):
    kind: str
    enabled: bool


class NotificationPreferencesIn(BaseModel):
    preferences: list[NotificationPreferenceOut]


class UserPreferenceIn(BaseModel):
    key: str = Field(min_length=1, max_length=64)
    value: Any


class OnboardingState(BaseModel):
    completed: bool
    follow_count: int
