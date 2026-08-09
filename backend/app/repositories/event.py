from sqlalchemy import Select, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import (
    Athlete,
    Competition,
    CompetitionEdition,
    Discipline,
    Event,
    Participation,
    Result,
)


def events_query(
    edition_id: int | None = None,
    discipline: str | None = None,
    status: str | None = None,
) -> Select:
    query = select(Event).options(selectinload(Event.discipline))
    if edition_id is not None:
        query = query.where(Event.edition_id == edition_id)
    if discipline:
        query = query.join(Discipline, Event.discipline_id == Discipline.id).where(
            Discipline.code == discipline
        )
    if status:
        query = query.where(Event.status == status)
    return query.order_by(Event.scheduled_start.asc().nulls_last(), Event.id.asc())


async def get_event_detail(session: AsyncSession, event_id: int) -> Event | None:
    result = await session.execute(
        select(Event)
        .where(Event.id == event_id)
        .options(
            selectinload(Event.discipline),
            selectinload(Event.edition)
            .selectinload(CompetitionEdition.competition)
            .selectinload(Competition.sport),
            selectinload(Event.edition).selectinload(CompetitionEdition.host_country),
            selectinload(Event.participations)
            .selectinload(Participation.athlete)
            .selectinload(Athlete.country),
            selectinload(Event.participations).selectinload(Participation.result),
        )
    )
    return result.scalar_one_or_none()


def sorted_participations(event: Event) -> list[Participation]:
    """Results order: ranked first (by position), then unranked by family name."""

    def sort_key(p: Participation) -> tuple:
        position = p.result.position if p.result else None
        return (position is None, position or 0, p.athlete.family_name)

    return sorted(event.participations, key=sort_key)


async def get_scheduled_event_with_participants(session: AsyncSession) -> Event | None:
    """Earliest scheduled event that has participants (dev simulator target)."""
    result = await session.execute(
        select(Event)
        .join(Participation)
        .where(Event.status == "scheduled")
        .options(selectinload(Event.participations).selectinload(Participation.result))
        .order_by(Event.scheduled_start.asc().nulls_last(), Event.id.asc())
        .limit(1)
    )
    return result.scalars().first()


async def get_result_for_participation(
    session: AsyncSession, participation_id: int
) -> Result | None:
    result = await session.execute(
        select(Result).where(Result.participation_id == participation_id)
    )
    return result.scalar_one_or_none()
