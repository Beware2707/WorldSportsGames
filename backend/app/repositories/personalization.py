"""Follows and per-user preferences.

``Favorite`` is entity-polymorphic (type + id) rather than one table per
followable thing, so "follow anything" stays uniform. The database cannot
enforce that pairing, so this module owns the integrity rules: every write
validates the target exists, and reads resolve display fields in one batched
query per type (never N+1).
"""

from collections.abc import Iterable

from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import (
    Athlete,
    Competition,
    Country,
    Discipline,
    Favorite,
    NotificationPreference,
    Sport,
    UserPreference,
)
from app.models.user import NOTIFICATION_KINDS

ONBOARDING_KEY = "onboarding_completed"

# entity_type -> model. Adding a followable type means adding a row here.
_MODELS = {
    "sport": Sport,
    "discipline": Discipline,
    "athlete": Athlete,
    "country": Country,
    "competition": Competition,
}


async def existing_entity_ids(
    session: AsyncSession, entity_type: str, ids: Iterable[int]
) -> set[int]:
    """Which of ``ids`` actually exist for ``entity_type``."""
    ids = list(ids)
    if not ids:
        return set()
    model = _MODELS[entity_type]
    rows = await session.execute(select(model.id).where(model.id.in_(ids)))
    return {row[0] for row in rows.all()}


async def list_favorites(session: AsyncSession, user_id: int) -> list[Favorite]:
    rows = await session.execute(
        select(Favorite)
        .where(Favorite.user_id == user_id)
        .order_by(Favorite.entity_type, Favorite.id)
    )
    return list(rows.scalars().all())


async def favorite_ids(
    session: AsyncSession, user_id: int, entity_type: str
) -> list[int]:
    rows = await session.execute(
        select(Favorite.entity_id).where(
            Favorite.user_id == user_id, Favorite.entity_type == entity_type
        )
    )
    return [row[0] for row in rows.all()]


async def add_favorite(
    session: AsyncSession, user_id: int, entity_type: str, entity_id: int
) -> bool:
    """Idempotent follow. Returns True when a new row was created."""
    existing = await session.execute(
        select(Favorite).where(
            Favorite.user_id == user_id,
            Favorite.entity_type == entity_type,
            Favorite.entity_id == entity_id,
        )
    )
    if existing.scalar_one_or_none() is not None:
        return False
    session.add(
        Favorite(user_id=user_id, entity_type=entity_type, entity_id=entity_id)
    )
    await session.flush()
    return True


async def remove_favorite(
    session: AsyncSession, user_id: int, entity_type: str, entity_id: int
) -> bool:
    """Idempotent unfollow. Returns True when a row was deleted."""
    result = await session.execute(
        delete(Favorite).where(
            Favorite.user_id == user_id,
            Favorite.entity_type == entity_type,
            Favorite.entity_id == entity_id,
        )
    )
    return bool(result.rowcount)


async def replace_favorites(
    session: AsyncSession, user_id: int, wanted: list[tuple[str, int]]
) -> int:
    """Set the user's follows to exactly ``wanted`` (onboarding submit).

    Only the difference is written, so existing follows keep their created_at.
    """
    current = {(f.entity_type, f.entity_id) for f in await list_favorites(session, user_id)}
    target = set(wanted)

    for entity_type, entity_id in current - target:
        await remove_favorite(session, user_id, entity_type, entity_id)
    for entity_type, entity_id in target - current:
        await add_favorite(session, user_id, entity_type, entity_id)
    return len(target)


async def resolve_favorite_labels(
    session: AsyncSession, favorites: list[Favorite]
) -> dict[tuple[str, int], tuple[str, str | None, str | None]]:
    """Display (name, subtitle, slug) per follow, batched one query per type."""
    by_type: dict[str, list[int]] = {}
    for fav in favorites:
        by_type.setdefault(fav.entity_type, []).append(fav.entity_id)

    labels: dict[tuple[str, int], tuple[str, str | None, str | None]] = {}
    for entity_type, ids in by_type.items():
        model = _MODELS[entity_type]
        query = select(model).where(model.id.in_(ids))
        if entity_type == "athlete":
            query = query.options(selectinload(Athlete.country))
        elif entity_type == "discipline":
            query = query.options(selectinload(Discipline.sport))
        elif entity_type == "competition":
            query = query.options(selectinload(Competition.sport))

        for row in (await session.execute(query)).scalars().unique():
            match entity_type:
                case "athlete":
                    labels[(entity_type, row.id)] = (
                        row.full_name,
                        row.country.name if row.country else None,
                        row.slug,
                    )
                case "sport":
                    labels[(entity_type, row.id)] = (row.name, row.category, row.code)
                case "discipline":
                    labels[(entity_type, row.id)] = (
                        row.name,
                        row.sport.name if row.sport else None,
                        row.code,
                    )
                case "country":
                    labels[(entity_type, row.id)] = (row.name, row.iso3, row.iso3)
                case "competition":
                    labels[(entity_type, row.id)] = (
                        row.name,
                        row.sport.name if row.sport else "Multi-sport",
                        row.slug,
                    )
    return labels


# ---- preferences ---------------------------------------------------------


async def get_preference(session: AsyncSession, user_id: int, key: str):
    row = await session.execute(
        select(UserPreference).where(
            UserPreference.user_id == user_id, UserPreference.key == key
        )
    )
    return row.scalar_one_or_none()


async def set_preference(session: AsyncSession, user_id: int, key: str, value) -> None:
    existing = await get_preference(session, user_id, key)
    if existing is None:
        session.add(UserPreference(user_id=user_id, key=key, value=value))
    else:
        existing.value = value
    await session.flush()


async def notification_preferences(
    session: AsyncSession, user_id: int
) -> dict[str, bool]:
    """Every known kind, defaulting to enabled when no opt-out row exists."""
    rows = await session.execute(
        select(NotificationPreference).where(NotificationPreference.user_id == user_id)
    )
    stored = {row.kind: row.enabled for row in rows.scalars()}
    return {kind: stored.get(kind, True) for kind in NOTIFICATION_KINDS}


async def set_notification_preferences(
    session: AsyncSession, user_id: int, updates: dict[str, bool]
) -> dict[str, bool]:
    rows = await session.execute(
        select(NotificationPreference).where(NotificationPreference.user_id == user_id)
    )
    existing = {row.kind: row for row in rows.scalars()}
    for kind, enabled in updates.items():
        if kind not in NOTIFICATION_KINDS:
            continue
        if kind in existing:
            existing[kind].enabled = enabled
        else:
            session.add(
                NotificationPreference(user_id=user_id, kind=kind, enabled=enabled)
            )
    await session.flush()
    return await notification_preferences(session, user_id)
