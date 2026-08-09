from sqlalchemy import func, select
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


async def append_update(
    session: AsyncSession, live_event_id: int, kind: str, payload: dict
) -> LiveUpdate:
    seq = await last_seq(session, live_event_id) + 1
    update = LiveUpdate(live_event_id=live_event_id, seq=seq, kind=kind, payload=payload)
    session.add(update)
    await session.flush()
    return update
