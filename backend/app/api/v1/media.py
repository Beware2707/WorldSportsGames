from datetime import UTC, datetime
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, Response, status
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from app.api.deps import CurrentUser, DbSession
from app.core.pagination import Page, PageParams, page_params, paginate
from app.models import Athlete, Country, MediaItem, Notification, Sport
from app.schemas.media import MediaItemOut, NotificationOut, UnreadCountOut

router = APIRouter(tags=["media"])


@router.get("/news", response_model=Page[MediaItemOut])
async def list_news(
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    sport: Annotated[str | None, Query()] = None,
    athlete: Annotated[str | None, Query()] = None,
    country: Annotated[str | None, Query(min_length=3, max_length=3)] = None,
) -> Page[MediaItemOut]:
    return await _media_page(session, params, "article", sport, athlete, country)


@router.get("/videos", response_model=Page[MediaItemOut])
async def list_videos(
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    sport: Annotated[str | None, Query()] = None,
    athlete: Annotated[str | None, Query()] = None,
    country: Annotated[str | None, Query(min_length=3, max_length=3)] = None,
) -> Page[MediaItemOut]:
    return await _media_page(session, params, "video", sport, athlete, country)


async def _media_page(
    session,
    params: PageParams,
    kind: str,
    sport: str | None,
    athlete: str | None,
    country: str | None,
) -> Page[MediaItemOut]:
    query = (
        select(MediaItem)
        .where(MediaItem.kind == kind)
        .options(
            selectinload(MediaItem.sports),
            selectinload(MediaItem.athletes),
            selectinload(MediaItem.countries),
        )
        .order_by(MediaItem.published_at.desc())
    )
    if sport:
        query = query.join(MediaItem.sports).where(Sport.code == sport)
    if athlete:
        query = query.join(MediaItem.athletes).where(Athlete.slug == athlete)
    if country:
        query = query.join(MediaItem.countries).where(Country.iso3 == country.upper())

    items, total, pages = await paginate(session, query, params)
    return Page(
        items=[MediaItemOut.from_model(item) for item in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/notifications", response_model=Page[NotificationOut])
async def list_notifications(
    session: DbSession,
    user: CurrentUser,
    params: Annotated[PageParams, Depends(page_params)],
    unread_only: Annotated[bool, Query()] = False,
) -> Page[NotificationOut]:
    query = (
        select(Notification)
        .where(Notification.user_id == user.id)
        .order_by(Notification.created_at.desc(), Notification.id.desc())
    )
    if unread_only:
        query = query.where(Notification.read_at.is_(None))

    items, total, pages = await paginate(session, query, params)
    return Page(
        items=[NotificationOut.model_validate(n) for n in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/notifications/unread-count", response_model=UnreadCountOut)
async def unread_count(session: DbSession, user: CurrentUser) -> UnreadCountOut:
    rows = await session.execute(
        select(Notification.id).where(
            Notification.user_id == user.id, Notification.read_at.is_(None)
        )
    )
    return UnreadCountOut(unread=len(rows.all()))


@router.post("/notifications/{notification_id}/read", response_model=NotificationOut)
async def mark_read(
    notification_id: int, session: DbSession, user: CurrentUser
) -> NotificationOut:
    notification = await session.get(Notification, notification_id)
    # Scoped to the owner: a wrong id must not reveal another user's inbox.
    if notification is None or notification.user_id != user.id:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Notification not found"
        )
    if notification.read_at is None:
        notification.read_at = datetime.now(UTC)
        await session.commit()
        await session.refresh(notification)
    return NotificationOut.model_validate(notification)


@router.post("/notifications/read-all", status_code=status.HTTP_204_NO_CONTENT)
async def mark_all_read(session: DbSession, user: CurrentUser) -> Response:
    rows = await session.execute(
        select(Notification).where(
            Notification.user_id == user.id, Notification.read_at.is_(None)
        )
    )
    now = datetime.now(UTC)
    for notification in rows.scalars():
        notification.read_at = now
    await session.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)
