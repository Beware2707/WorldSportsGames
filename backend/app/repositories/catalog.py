from sqlalchemy import Select, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Country, Discipline, Sport

SPORT_SORTS: dict[str, object] = {
    "name": Sport.name.asc(),
    "-name": Sport.name.desc(),
    "sort_order": Sport.sort_order.asc(),
}


def sports_query(category: str | None, sort: str | None) -> Select:
    query = select(Sport)
    if category:
        query = query.where(Sport.category == category)
    order = SPORT_SORTS.get(sort or "sort_order", Sport.sort_order.asc())
    return query.order_by(order, Sport.name.asc())


async def get_sport_by_code(session: AsyncSession, code: str) -> Sport | None:
    result = await session.execute(
        select(Sport).where(Sport.code == code).options(selectinload(Sport.disciplines))
    )
    return result.scalar_one_or_none()


def disciplines_query(sport_code: str | None) -> Select:
    query = select(Discipline).join(Sport).order_by(Sport.name.asc(), Discipline.name.asc())
    if sport_code:
        query = query.where(Sport.code == sport_code)
    return query


def countries_query(sort: str | None) -> Select:
    order = Country.name.desc() if sort == "-name" else Country.name.asc()
    return select(Country).order_by(order)
