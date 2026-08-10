"""Rankings, records, medals and profile aggregates.

All list reads batch their lookups: a ladder resolves every entity name in one
query per scope, and the medal table aggregates in SQL rather than in Python.
"""

from datetime import date

from sqlalchemy import Select, func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import (
    Athlete,
    Competition,
    CompetitionEdition,
    Country,
    Discipline,
    Event,
    Medal,
    Participation,
    Ranking,
    Record,
    Result,
    Team,
)


async def latest_as_of(
    session: AsyncSession, scope: str, methodology: str, discipline_id: int | None
) -> date | None:
    """Most recent snapshot date for a ladder — ranking data is versioned by
    ``as_of``, so mixing dates would interleave two different tables."""
    query = select(func.max(Ranking.as_of)).where(
        Ranking.scope == scope, Ranking.methodology == methodology
    )
    if discipline_id is not None:
        query = query.where(Ranking.discipline_id == discipline_id)
    return (await session.execute(query)).scalar_one_or_none()


async def ranking_entries(
    session: AsyncSession,
    scope: str,
    methodology: str,
    discipline_id: int | None,
    as_of: date | None,
    limit: int = 50,
) -> list[Ranking]:
    query = select(Ranking).where(
        Ranking.scope == scope, Ranking.methodology == methodology
    )
    if discipline_id is not None:
        query = query.where(Ranking.discipline_id == discipline_id)
    if as_of is not None:
        query = query.where(Ranking.as_of == as_of)
    query = query.order_by(Ranking.rank).limit(limit)
    return list((await session.execute(query)).scalars().all())


async def resolve_ranked_entities(
    session: AsyncSession, scope: str, ids: list[int]
) -> dict[int, tuple[str, str | None, str | None]]:
    """(name, subtitle, slug) per ranked entity — one query, never per row."""
    if not ids:
        return {}
    out: dict[int, tuple[str, str | None, str | None]] = {}
    match scope:
        case "athlete":
            rows = (
                await session.execute(
                    select(Athlete)
                    .where(Athlete.id.in_(ids))
                    .options(selectinload(Athlete.country))
                )
            ).scalars()
            for a in rows:
                out[a.id] = (a.full_name, a.country.name if a.country else None, a.slug)
        case "country":
            rows = (
                await session.execute(select(Country).where(Country.id.in_(ids)))
            ).scalars()
            for c in rows:
                out[c.id] = (c.name, c.iso3, c.iso3)
        case "team":
            rows = (
                await session.execute(
                    select(Team)
                    .where(Team.id.in_(ids))
                    .options(selectinload(Team.country), selectinload(Team.sport))
                )
            ).scalars()
            for t in rows:
                out[t.id] = (t.name, t.country.name if t.country else None, None)
    return out


def records_query(
    kind: str | None = None,
    discipline_code: str | None = None,
    gender: str | None = None,
) -> Select:
    query = (
        select(Record)
        .options(
            selectinload(Record.discipline).selectinload(Discipline.sport),
            selectinload(Record.country),
        )
        .join(Discipline, Record.discipline_id == Discipline.id)
    )
    if kind:
        query = query.where(Record.kind == kind)
    if discipline_code:
        query = query.where(Discipline.code == discipline_code)
    if gender:
        query = query.where(Record.gender == gender)
    return query.order_by(Discipline.name, Record.event_name, Record.kind)


async def record_holders(
    session: AsyncSession, records: list[Record]
) -> dict[int, tuple[str, str | None]]:
    """Holder display name per record id, batched by holder type."""
    athlete_ids = [r.athlete_id for r in records if r.athlete_id]
    team_ids = [r.team_id for r in records if r.team_id]
    holders: dict[int, tuple[str, str | None]] = {}

    if athlete_ids:
        rows = (
            await session.execute(select(Athlete).where(Athlete.id.in_(athlete_ids)))
        ).scalars()
        by_id = {a.id: a for a in rows}
        for r in records:
            if r.athlete_id in by_id:
                a = by_id[r.athlete_id]
                holders[r.id] = (a.full_name, a.slug)
    if team_ids:
        rows = (
            await session.execute(select(Team).where(Team.id.in_(team_ids)))
        ).scalars()
        by_id = {t.id: t for t in rows}
        for r in records:
            if r.team_id in by_id:
                holders[r.id] = (by_id[r.team_id].name, None)
    return holders


async def medal_table(
    session: AsyncSession, edition_id: int | None = None
) -> list[tuple[Country, int, int, int]]:
    """(country, gold, silver, bronze) aggregated in SQL, Olympic-ordered.

    Ordering is gold-first then silver then bronze — the standard table
    convention — not by total, which would rank differently.
    """
    gold = func.count().filter(Medal.metal == "gold")
    silver = func.count().filter(Medal.metal == "silver")
    bronze = func.count().filter(Medal.metal == "bronze")

    query = (
        select(Country, gold.label("g"), silver.label("s"), bronze.label("b"))
        .join(Medal, Medal.country_id == Country.id)
        .group_by(Country.id)
        .order_by(gold.desc(), silver.desc(), bronze.desc(), Country.name)
    )
    if edition_id is not None:
        query = query.where(Medal.edition_id == edition_id)
    return [(row[0], row[1], row[2], row[3]) for row in (await session.execute(query)).all()]


async def country_medal_tally(
    session: AsyncSession, country_id: int
) -> tuple[int, int, int]:
    row = (
        await session.execute(
            select(
                func.count().filter(Medal.metal == "gold"),
                func.count().filter(Medal.metal == "silver"),
                func.count().filter(Medal.metal == "bronze"),
            ).where(Medal.country_id == country_id)
        )
    ).one()
    return (row[0] or 0, row[1] or 0, row[2] or 0)


async def get_country_by_iso3(session: AsyncSession, iso3: str) -> Country | None:
    return (
        await session.execute(select(Country).where(Country.iso3 == iso3.upper()))
    ).scalar_one_or_none()


async def athlete_medals(session: AsyncSession, athlete_id: int) -> list[Medal]:
    rows = await session.execute(
        select(Medal)
        .where(Medal.athlete_id == athlete_id)
        .options(
            selectinload(Medal.edition).selectinload(CompetitionEdition.competition),
            selectinload(Medal.discipline),
        )
        .order_by(Medal.id)
    )
    return list(rows.scalars().all())


async def athlete_records(session: AsyncSession, athlete_id: int) -> list[Record]:
    rows = await session.execute(
        select(Record)
        .where(Record.athlete_id == athlete_id)
        .options(
            selectinload(Record.discipline).selectinload(Discipline.sport),
            selectinload(Record.country),
        )
        .order_by(Record.kind, Record.event_name)
    )
    return list(rows.scalars().all())


async def athlete_recent_results(
    session: AsyncSession, athlete_id: int, limit: int = 10
) -> list[tuple[Result, Event]]:
    rows = await session.execute(
        select(Result, Event)
        .join(Participation, Result.participation_id == Participation.id)
        .join(Event, Participation.event_id == Event.id)
        .where(Participation.athlete_id == athlete_id)
        .options(
            selectinload(Event.discipline),
            selectinload(Event.edition).selectinload(CompetitionEdition.competition),
        )
        .order_by(Event.scheduled_start.desc().nulls_last(), Event.id.desc())
        .limit(limit)
    )
    return [(row[0], row[1]) for row in rows.all()]


async def athlete_rankings(session: AsyncSession, athlete_id: int) -> list[Ranking]:
    rows = await session.execute(
        select(Ranking)
        .where(Ranking.scope == "athlete", Ranking.entity_id == athlete_id)
        .options(selectinload(Ranking.discipline), selectinload(Ranking.sport))
        .order_by(Ranking.as_of.desc(), Ranking.rank)
    )
    return list(rows.scalars().all())


async def country_athletes(
    session: AsyncSession, country_id: int, limit: int = 30
) -> list[Athlete]:
    rows = await session.execute(
        select(Athlete)
        .where(Athlete.country_id == country_id)
        .options(selectinload(Athlete.country))
        .order_by(Athlete.family_name)
        .limit(limit)
    )
    return list(rows.scalars().all())


async def country_records(
    session: AsyncSession, country_id: int, limit: int = 20
) -> list[Record]:
    rows = await session.execute(
        select(Record)
        .where(Record.country_id == country_id)
        .options(
            selectinload(Record.discipline).selectinload(Discipline.sport),
            selectinload(Record.country),
        )
        .order_by(Record.kind, Record.event_name)
        .limit(limit)
    )
    return list(rows.scalars().all())


async def editions_with_medals(session: AsyncSession) -> list[CompetitionEdition]:
    rows = await session.execute(
        select(CompetitionEdition)
        .join(Medal, Medal.edition_id == CompetitionEdition.id)
        .options(
            selectinload(CompetitionEdition.competition).selectinload(Competition.sport),
            # EditionOut serializes host_country; without this the lazy load
            # fires outside the greenlet and raises MissingGreenlet.
            selectinload(CompetitionEdition.host_country),
        )
        .group_by(CompetitionEdition.id)
        .order_by(CompetitionEdition.year.desc())
    )
    return list(rows.scalars().all())
