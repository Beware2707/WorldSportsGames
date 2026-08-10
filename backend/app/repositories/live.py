from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Competition, CompetitionEdition, Event, LiveEvent, LiveUpdate


async def get_live_events(session: AsyncSession) -> list[LiveEvent]:
    result = await session.execute(
        select(LiveEvent)
        .where(LiveEvent.status == "live")
        .options(
            selectinload(LiveEvent.event).selectinload(Event.discipline),
            selectinload(LiveEvent.event)
            .selectinload(Event.edition)
            .selectinload(CompetitionEdition.competition)
            .selectinload(Competition.sport),
        )
        .order_by(LiveEvent.updated_at.desc())
    )
    return list(result.scalars().all())


async def get_live_for_event(session: AsyncSession, event_id: int) -> LiveEvent | None:
    result = await session.execute(select(LiveEvent).where(LiveEvent.event_id == event_id))
    return result.scalar_one_or_none()


async def last_seq(session: AsyncSession, live_event_id: int) -> int:
    result = await session.execute(
        select(func.max(LiveUpdate.seq)).where(LiveUpdate.live_event_id == live_event_id)
    )
    return result.scalar_one() or 0


async def last_seq_map(
    session: AsyncSession, live_event_ids: list[int]
) -> dict[int, int]:
    """Latest seq for many live events in ONE query (avoids N+1 on the hot
    live-read path: GET /live and every WebSocket connect)."""
    if not live_event_ids:
        return {}
    result = await session.execute(
        select(LiveUpdate.live_event_id, func.max(LiveUpdate.seq))
        .where(LiveUpdate.live_event_id.in_(live_event_ids))
        .group_by(LiveUpdate.live_event_id)
    )
    return {row[0]: row[1] or 0 for row in result.all()}


async def append_update(
    session: AsyncSession, live_event_id: int, kind: str, payload: dict
) -> LiveUpdate:
    """Append the next update in sequence.

    seq allocation is read-then-insert, so two concurrent producers for the
    same live event can pick the same seq; ``uq_live_update_seq`` rejects the
    loser. Retry on that collision instead of aborting the producer mid-run
    and stranding the event in ``live``.
    """
    for attempt in range(_SEQ_RETRIES):
        seq = await last_seq(session, live_event_id) + 1
        update = LiveUpdate(
            live_event_id=live_event_id, seq=seq, kind=kind, payload=payload
        )
        session.add(update)
        try:
            await session.flush()
            return update
        except IntegrityError:
            await session.rollback()
            if attempt == _SEQ_RETRIES - 1:
                raise
    raise RuntimeError("unreachable")  # pragma: no cover


_SEQ_RETRIES = 5
