from sqlalchemy import Select, or_, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Athlete, Country, Discipline, Sport, athlete_discipline


def athletes_query(
    sport: str | None = None,
    discipline: str | None = None,
    country: str | None = None,
    q: str | None = None,
    sort: str | None = None,
) -> Select:
    query = select(Athlete).options(selectinload(Athlete.country))

    if sport or discipline:
        query = query.join(athlete_discipline).join(Discipline)
        if discipline:
            query = query.where(Discipline.code == discipline)
        if sport:
            query = query.join(Sport).where(Sport.code == sport)
        query = query.distinct()
    if country:
        query = query.join(Country, Athlete.country_id == Country.id).where(
            Country.iso3 == country.upper()
        )
    if q:
        pattern = f"%{q}%"
        query = query.where(
            or_(Athlete.given_name.ilike(pattern), Athlete.family_name.ilike(pattern))
        )

    if sort == "-name":
        query = query.order_by(Athlete.family_name.desc(), Athlete.given_name.desc())
    else:
        query = query.order_by(Athlete.family_name.asc(), Athlete.given_name.asc())
    return query


async def get_athlete_by_slug(session: AsyncSession, slug: str) -> Athlete | None:
    result = await session.execute(
        select(Athlete)
        .where(Athlete.slug == slug)
        .options(selectinload(Athlete.country), selectinload(Athlete.disciplines))
    )
    return result.scalar_one_or_none()
