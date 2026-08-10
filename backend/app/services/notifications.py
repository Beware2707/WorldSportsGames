"""Notification generation and delivery.

Two invariants:

- **Preferences are checked before a row is written**, so a disabled kind
  leaves no trace anywhere — not in an inbox, not in a delivery queue.
- **Generation is idempotent** via ``dedupe_key``. Re-running ingestion after
  a retry or a restart must not notify anyone twice.

Delivery today is the in-app inbox. Push transports plug in behind
``NotificationTransport`` without changing generation.
"""

import logging
from typing import Protocol

from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import Favorite, Notification, NotificationPreference

logger = logging.getLogger(__name__)


class NotificationTransport(Protocol):
    """An outbound channel. The inbox is always written; transports are extra."""

    name: str

    async def send(self, user_id: int, notification: Notification) -> None: ...


_transports: list[NotificationTransport] = []


def register_transport(transport: NotificationTransport) -> None:
    _transports.append(transport)


async def disabled_recipients(
    session: AsyncSession, user_ids: list[int], kind: str
) -> set[int]:
    """Which of ``user_ids`` have opted out of ``kind`` — in one query.

    Preferences are opt-out: absence of a row means enabled, so only explicit
    ``enabled=False`` rows matter here.
    """
    if not user_ids:
        return set()
    rows = await session.execute(
        select(NotificationPreference.user_id).where(
            NotificationPreference.user_id.in_(user_ids),
            NotificationPreference.kind == kind,
            NotificationPreference.enabled.is_(False),
        )
    )
    return {row[0] for row in rows.all()}


async def followers_of(
    session: AsyncSession, entity_type: str, entity_id: int
) -> list[int]:
    rows = await session.execute(
        select(Favorite.user_id).where(
            Favorite.entity_type == entity_type, Favorite.entity_id == entity_id
        )
    )
    return [row[0] for row in rows.all()]


async def notify(
    session: AsyncSession,
    *,
    user_ids: list[int],
    kind: str,
    title: str,
    body: str,
    payload: dict,
    dedupe_key: str,
) -> list[Notification]:
    """Create notifications for users who have this kind enabled.

    Returns only the rows actually created — recipients who opted out, or who
    already received this exact notification, are silently skipped.
    """
    recipients = list(dict.fromkeys(user_ids))  # de-duplicate, preserve order
    if not recipients:
        return []

    # One query for every recipient's preferences, not one per recipient:
    # a medal for a popular athlete fans out to thousands of followers.
    opted_out = await disabled_recipients(session, recipients, kind)
    wanted = [uid for uid in recipients if uid not in opted_out]
    if not wanted:
        return []

    created: list[Notification] = []
    for user_id in wanted:
        notification = Notification(
            user_id=user_id,
            kind=kind,
            title=title,
            body=body,
            payload=payload,
            dedupe_key=dedupe_key,
        )
        try:
            # SAVEPOINT, not a bare flush: a duplicate for ONE recipient must
            # undo only that insert. A full session.rollback() here would
            # discard every notification already created in this transaction
            # — and any caller work in it, such as the medal row that
            # triggered the fan-out.
            async with session.begin_nested():
                session.add(notification)
                await session.flush()
        except IntegrityError:
            # Already delivered to this user — the unique constraint is the
            # source of truth, so a concurrent generator cannot double-send.
            continue
        created.append(notification)

    for notification in created:
        for transport in _transports:
            try:
                await transport.send(notification.user_id, notification)
            except Exception:
                # A failed push must not lose the inbox row.
                logger.warning(
                    "transport %s failed for notification %s",
                    getattr(transport, "name", "?"),
                    notification.id,
                )
    return created


async def notify_medal(
    session: AsyncSession,
    *,
    athlete_id: int,
    athlete_name: str,
    athlete_slug: str,
    country_id: int,
    metal: str,
    event_name: str,
) -> list[Notification]:
    """Fan a medal out to followers of the athlete and of their country."""
    recipients = await followers_of(session, "athlete", athlete_id)
    recipients += await followers_of(session, "country", country_id)
    return await notify(
        session,
        user_ids=recipients,
        kind="medal_result",
        title=f"{metal.title()} for {athlete_name}",
        body=f"{athlete_name} took {metal} in {event_name}.",
        payload={"route": f"/athletes/{athlete_slug}"},
        dedupe_key=f"medal:{athlete_id}:{event_name}:{metal}",
    )


async def notify_record(
    session: AsyncSession,
    *,
    record_id: int,
    athlete_id: int | None,
    athlete_name: str,
    athlete_slug: str | None,
    kind_label: str,
    event_name: str,
    value_text: str,
    discipline_id: int | None,
) -> list[Notification]:
    recipients: list[int] = []
    if athlete_id is not None:
        recipients += await followers_of(session, "athlete", athlete_id)
    if discipline_id is not None:
        recipients += await followers_of(session, "discipline", discipline_id)
    return await notify(
        session,
        user_ids=recipients,
        kind="record_broken",
        title=f"{kind_label} in {event_name}",
        body=f"{athlete_name} set {value_text} in {event_name}.",
        payload={"route": f"/athletes/{athlete_slug}"} if athlete_slug else {},
        dedupe_key=f"record:{record_id}",
    )


async def notify_followed_athlete_result(
    session: AsyncSession,
    *,
    athlete_id: int,
    athlete_name: str,
    athlete_slug: str,
    event_id: int,
    event_name: str,
    position: int | None,
    value_text: str | None,
) -> list[Notification]:
    placing = (
        f"finished {position}" if position else "recorded a result"
    )
    detail = f" ({value_text})" if value_text else ""
    return await notify(
        session,
        user_ids=await followers_of(session, "athlete", athlete_id),
        kind="followed_athlete_result",
        title=f"{athlete_name} — {event_name}",
        body=f"{athlete_name} {placing} in {event_name}{detail}.",
        payload={"route": f"/events/{event_id}"},
        dedupe_key=f"result:{athlete_id}:{event_id}",
    )
