from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.text import LIKE_ESCAPE, like_pattern
from app.models import Athlete, Competition, Country, Sport
from app.schemas.athlete import AthleteOut
from app.schemas.catalog import CountryOut, SportOut
from app.schemas.competition import CompetitionOut
from app.schemas.search import SearchResults, Suggestion, Suggestions

_PER_CATEGORY = 5
_SUGGESTION_LIMIT = 8


def _ilike(column, pattern: str):
    return column.ilike(pattern, escape=LIKE_ESCAPE)


async def global_search(session: AsyncSession, q: str) -> SearchResults:
    pattern = like_pattern(q)

    sports = (
        (
            await session.execute(
                select(Sport).where(_ilike(Sport.name, pattern)).limit(_PER_CATEGORY)
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
                        _ilike(Athlete.given_name, pattern),
                        _ilike(Athlete.family_name, pattern),
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
                .where(or_(_ilike(Country.name, pattern), _ilike(Country.iso3, pattern)))
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
                .where(_ilike(Competition.name, pattern))
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


async def suggest(session: AsyncSession, q: str) -> Suggestions:
    """Flat, ranked autocomplete list for the search bar.

    Prefix matches rank above contains-matches so typing "ath" surfaces
    "Athletics" before "Marathon Swimming".
    """
    pattern = like_pattern(q)
    prefix = f"{q.lower()}"
    items: list[Suggestion] = []

    for sport in (
        (
            await session.execute(
                select(Sport).where(_ilike(Sport.name, pattern)).limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    ):
        items.append(
            Suggestion(
                kind="sport", id=sport.id, label=sport.name, sublabel=sport.category,
                slug=sport.code,
            )
        )

    for athlete in (
        (
            await session.execute(
                select(Athlete)
                .where(
                    or_(
                        _ilike(Athlete.given_name, pattern),
                        _ilike(Athlete.family_name, pattern),
                    )
                )
                .options(selectinload(Athlete.country))
                .limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    ):
        items.append(
            Suggestion(
                kind="athlete",
                id=athlete.id,
                label=athlete.full_name,
                sublabel=athlete.country.name if athlete.country else None,
                slug=athlete.slug,
            )
        )

    for competition in (
        (
            await session.execute(
                select(Competition)
                .where(_ilike(Competition.name, pattern))
                .limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    ):
        items.append(
            Suggestion(
                kind="competition",
                id=competition.id,
                label=competition.name,
                sublabel=competition.level,
                slug=competition.slug,
            )
        )

    for country in (
        (
            await session.execute(
                select(Country)
                .where(or_(_ilike(Country.name, pattern), _ilike(Country.iso3, pattern)))
                .limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    ):
        items.append(
            Suggestion(
                kind="country", id=country.id, label=country.name,
                sublabel=country.iso3, slug=country.iso3,
            )
        )

    items.sort(key=lambda s: (not s.label.lower().startswith(prefix), s.label.lower()))
    return Suggestions(query=q, items=items[:_SUGGESTION_LIMIT])
