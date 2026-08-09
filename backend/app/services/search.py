from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Athlete, Competition, Country, Sport
from app.schemas.athlete import AthleteOut
from app.schemas.catalog import CountryOut, SportOut
from app.schemas.competition import CompetitionOut
from app.schemas.search import SearchResults

_PER_CATEGORY = 5


async def global_search(session: AsyncSession, q: str) -> SearchResults:
    pattern = f"%{q}%"

    sports = (
        (
            await session.execute(
                select(Sport).where(Sport.name.ilike(pattern)).limit(_PER_CATEGORY)
            )
        )
        .scalars()
        .all()
    )
    athletes = (
        (
            await session.execute(
                select(Athlete)
                .where(
                    or_(
                        Athlete.given_name.ilike(pattern),
                        Athlete.family_name.ilike(pattern),
                    )
                )
                .options(selectinload(Athlete.country))
                .limit(_PER_CATEGORY)
            )
        )
        .scalars()
        .all()
    )
    countries = (
        (
            await session.execute(
                select(Country)
                .where(or_(Country.name.ilike(pattern), Country.iso3.ilike(pattern)))
                .limit(_PER_CATEGORY)
            )
        )
        .scalars()
        .all()
    )
    competitions = (
        (
            await session.execute(
                select(Competition)
                .where(Competition.name.ilike(pattern))
                .options(selectinload(Competition.sport))
                .limit(_PER_CATEGORY)
            )
        )
        .scalars()
        .all()
    )

    return SearchResults(
        query=q,
        sports=[SportOut.model_validate(s) for s in sports],
        athletes=[AthleteOut.model_validate(a) for a in athletes],
        countries=[CountryOut.model_validate(c) for c in countries],
        competitions=[CompetitionOut.model_validate(c) for c in competitions],
    )
