from sqlalchemy import case, or_, select
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


SPORT_CATEGORY_LABELS = {
    "summer": "Summer Games",
    "winter": "Winter Games",
    "la28": "LA28 addition",
}

COMPETITION_LEVEL_LABELS = {
    "olympic": "Olympic",
    "world": "World championship",
    "continental": "Continental",
    "league": "League",
    "national": "National",
    "other": "Competition",
}


def _prefix_first(column, prefix_pattern: str):
    """ORDER BY term putting prefix matches ahead of contains-matches.

    Ranking must happen in SQL: ordering a LIMITed sample in Python only
    reshuffles whichever rows the database happened to return, so a true
    prefix match beyond that sample would never be a candidate at all.
    """
    return case((column.ilike(prefix_pattern, escape=LIKE_ESCAPE), 0), else_=1)


async def suggest(session: AsyncSession, q: str) -> Suggestions:
    """Flat, ranked autocomplete list for the search bar.

    Prefix matches rank above contains-matches so typing "ath" surfaces
    "Athletics" before "Marathon Swimming", and the merge interleaves kinds so
    one crowded category cannot evict every athlete or competition.
    """
    pattern = like_pattern(q)
    prefix_pattern = like_pattern(q)[1:]  # "abc%" — anchored at the start
    prefix = q.lower()

    sports = (
        (
            await session.execute(
                select(Sport)
                .where(_ilike(Sport.name, pattern))
                .order_by(_prefix_first(Sport.name, prefix_pattern), Sport.name)
                .limit(_SUGGESTION_LIMIT)
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
                .order_by(
                    _prefix_first(Athlete.family_name, prefix_pattern),
                    _prefix_first(Athlete.given_name, prefix_pattern),
                    Athlete.family_name,
                )
                .limit(_SUGGESTION_LIMIT)
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
                .order_by(
                    _prefix_first(Competition.name, prefix_pattern), Competition.name
                )
                .limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    )
    countries = (
        (
            await session.execute(
                select(Country)
                .where(
                    or_(_ilike(Country.name, pattern), _ilike(Country.iso3, pattern))
                )
                .order_by(_prefix_first(Country.name, prefix_pattern), Country.name)
                .limit(_SUGGESTION_LIMIT)
            )
        )
        .scalars()
        .all()
    )

    by_kind: list[list[Suggestion]] = [
        [
            Suggestion(
                kind="sport",
                id=s.id,
                label=s.name,
                sublabel=SPORT_CATEGORY_LABELS.get(s.category, s.category),
                slug=s.code,
            )
            for s in sports
        ],
        [
            Suggestion(
                kind="athlete",
                id=a.id,
                label=a.full_name,
                sublabel=a.country.name if a.country else None,
                slug=a.slug,
            )
            for a in athletes
        ],
        [
            Suggestion(
                kind="competition",
                id=c.id,
                label=c.name,
                sublabel=COMPETITION_LEVEL_LABELS.get(c.level, c.level),
                slug=c.slug,
            )
            for c in competitions
        ],
        [
            Suggestion(
                kind="country",
                id=c.id,
                label=c.name,
                sublabel=c.iso3,
                slug=c.iso3,
            )
            for c in countries
        ],
    ]

    # Prefix matches first, then contains-matches; within each tier, round-robin
    # across kinds so a crowded category cannot evict every other kind.
    def is_prefix(item: Suggestion) -> bool:
        return item.label.lower().startswith(prefix)

    items: list[Suggestion] = []
    for tier in (True, False):
        queues = [[i for i in kind_items if is_prefix(i) is tier] for kind_items in by_kind]
        while len(items) < _SUGGESTION_LIMIT and any(queues):
            for queue in queues:
                if not queue:
                    continue
                items.append(queue.pop(0))
                if len(items) >= _SUGGESTION_LIMIT:
                    break

    return Suggestions(query=q, items=items[:_SUGGESTION_LIMIT])
