from sqlalchemy import Select, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Competition, CompetitionEdition, Sport


def competitions_query(
    level: str | None = None, sport: str | None = None, sort: str | None = None
) -> Select:
    query = select(Competition).options(selectinload(Competition.sport))
    if level:
        query = query.where(Competition.level == level)
    if sport:
        query = query.join(Sport, Competition.sport_id == Sport.id).where(Sport.code == sport)
    order = Competition.name.desc() if sort == "-name" else Competition.name.asc()
    return query.order_by(order)


async def get_competition_by_slug(session: AsyncSession, slug: str) -> Competition | None:
    result = await session.execute(
        select(Competition)
        .where(Competition.slug == slug)
        .options(
            selectinload(Competition.sport),
            selectinload(Competition.editions).selectinload(CompetitionEdition.host_country),
        )
    )
    return result.scalar_one_or_none()


async def upcoming_editions(session: AsyncSession, limit: int = 5) -> list[CompetitionEdition]:
    result = await session.execute(
        select(CompetitionEdition)
        .where(CompetitionEdition.status == "upcoming")
        .order_by(CompetitionEdition.start_date.asc().nulls_last())
        .options(
            selectinload(CompetitionEdition.competition).selectinload(Competition.sport),
            selectinload(CompetitionEdition.host_country),
        )
        .limit(limit)
    )
    return list(result.scalars().all())


async def live_editions(session: AsyncSession, limit: int = 10) -> list[CompetitionEdition]:
    result = await session.execute(
        select(CompetitionEdition)
        .where(CompetitionEdition.status == "live")
        .order_by(CompetitionEdition.start_date.asc().nulls_last())
        .options(
            selectinload(CompetitionEdition.competition).selectinload(Competition.sport),
            selectinload(CompetitionEdition.host_country),
        )
        .limit(limit)
    )
    return list(result.scalars().all())


async def featured_competitions(session: AsyncSession, limit: int = 6) -> list[Competition]:
    result = await session.execute(
        select(Competition)
        .where(Competition.level.in_(["olympic", "world"]))
        .options(selectinload(Competition.sport))
        .order_by(Competition.level.asc(), Competition.name.asc())
        .limit(limit)
    )
    return list(result.scalars().all())
