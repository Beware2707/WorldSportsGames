from fastapi import APIRouter, HTTPException, Response, status

from app.api.deps import CurrentUser, DbSession
from app.repositories.personalization import (
    ONBOARDING_KEY,
    add_favorite,
    existing_entity_ids,
    get_preference,
    list_favorites,
    notification_preferences,
    remove_favorite,
    replace_favorites,
    resolve_favorite_labels,
    set_notification_preferences,
    set_preference,
)
from app.schemas.personalization import (
    FavoriteIn,
    FavoriteOut,
    FavoritesBulkIn,
    NotificationPreferenceOut,
    NotificationPreferencesIn,
    OnboardingState,
)

router = APIRouter(prefix="/users/me", tags=["users"])


async def _favorites_response(session, user_id: int) -> list[FavoriteOut]:
    favorites = await list_favorites(session, user_id)
    labels = await resolve_favorite_labels(session, favorites)
    out = []
    for fav in favorites:
        label = labels.get((fav.entity_type, fav.entity_id))
        out.append(
            FavoriteOut(
                entity_type=fav.entity_type,
                entity_id=fav.entity_id,
                name=label[0] if label else None,
                subtitle=label[1] if label else None,
                slug=label[2] if label else None,
            )
        )
    return out


@router.get("/favorites", response_model=list[FavoriteOut])
async def get_favorites(session: DbSession, user: CurrentUser) -> list[FavoriteOut]:
    return await _favorites_response(session, user.id)


@router.post("/favorites", response_model=list[FavoriteOut], status_code=status.HTTP_201_CREATED)
async def follow(body: FavoriteIn, session: DbSession, user: CurrentUser) -> list[FavoriteOut]:
    """Follow an entity. Idempotent — following twice is not an error."""
    if not await existing_entity_ids(session, body.entity_type, [body.entity_id]):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"No {body.entity_type} with id {body.entity_id}",
        )
    await add_favorite(session, user.id, body.entity_type, body.entity_id)
    await session.commit()
    return await _favorites_response(session, user.id)


@router.delete("/favorites/{entity_type}/{entity_id}", status_code=status.HTTP_204_NO_CONTENT)
async def unfollow(
    entity_type: str, entity_id: int, session: DbSession, user: CurrentUser
) -> Response:
    """Unfollow. Idempotent — unfollowing something you don't follow is fine."""
    await remove_favorite(session, user.id, entity_type, entity_id)
    await session.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)


@router.put("/favorites", response_model=list[FavoriteOut])
async def set_favorites(
    body: FavoritesBulkIn, session: DbSession, user: CurrentUser
) -> list[FavoriteOut]:
    """Replace the whole follow set (onboarding submit).

    Unknown entity ids are rejected as a group rather than silently dropped —
    a follow list that quietly loses entries is worse than a failed submit.
    """
    wanted: list[tuple[str, int]] = []
    by_type: dict[str, list[int]] = {}
    for item in body.favorites:
        by_type.setdefault(item.entity_type, []).append(item.entity_id)
        wanted.append((item.entity_type, item.entity_id))

    for entity_type, ids in by_type.items():
        found = await existing_entity_ids(session, entity_type, ids)
        missing = sorted(set(ids) - found)
        if missing:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Unknown {entity_type} ids: {missing}",
            )

    await replace_favorites(session, user.id, wanted)
    await set_preference(session, user.id, ONBOARDING_KEY, True)
    await session.commit()
    return await _favorites_response(session, user.id)


@router.get("/onboarding", response_model=OnboardingState)
async def onboarding_state(session: DbSession, user: CurrentUser) -> OnboardingState:
    pref = await get_preference(session, user.id, ONBOARDING_KEY)
    favorites = await list_favorites(session, user.id)
    return OnboardingState(
        completed=bool(pref.value) if pref is not None else False,
        follow_count=len(favorites),
    )


@router.get("/notifications", response_model=list[NotificationPreferenceOut])
async def get_notification_preferences(
    session: DbSession, user: CurrentUser
) -> list[NotificationPreferenceOut]:
    prefs = await notification_preferences(session, user.id)
    return [NotificationPreferenceOut(kind=k, enabled=v) for k, v in prefs.items()]


@router.put("/notifications", response_model=list[NotificationPreferenceOut])
async def update_notification_preferences(
    body: NotificationPreferencesIn, session: DbSession, user: CurrentUser
) -> list[NotificationPreferenceOut]:
    updated = await set_notification_preferences(
        session, user.id, {p.kind: p.enabled for p in body.preferences}
    )
    await session.commit()
    return [NotificationPreferenceOut(kind=k, enabled=v) for k, v in updated.items()]
