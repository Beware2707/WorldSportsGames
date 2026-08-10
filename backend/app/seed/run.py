"""Idempotent database seeder.

Usage:  python -m app.seed.run [--fixtures]

Always seeds the sport/discipline taxonomy and reference catalogue
(countries, major competitions). Fictional development athletes are seeded
only with ``--fixtures`` AND ``SPORTS_ENABLE_DEV_FIXTURES=true`` — a
deliberate double gate so demo data cannot reach production by accident.
"""

import argparse
import asyncio
import sys
from datetime import date, datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.config import get_settings
from app.db.session import get_session_factory
from app.models import (
    Achievement,
    Athlete,
    Competition,
    CompetitionEdition,
    Country,
    Discipline,
    Event,
    Game,
    Medal,
    Participation,
    Ranking,
    Record,
    Result,
    Sport,
)
from app.seed.fixtures import (
    ACHIEVEMENTS,
    COMPETITIONS,
    COUNTRIES,
    DEV_ATHLETES,
    DEV_BIO_PREFIX,
    DEV_EVENTS,
    DEV_MEDALS,
    DEV_RANKINGS,
    DEV_RECORDS,
    GAMES,
    RANKING_AS_OF,
)
from app.seed.taxonomy import ICONS, SPORTS


async def seed_taxonomy(session: AsyncSession) -> int:
    existing = {s.code: s for s in (await session.execute(select(Sport))).scalars()}
    created = 0
    for order, (code, name, category, disciplines) in enumerate(SPORTS):
        sport = existing.get(code)
        if sport is None:
            sport = Sport(code=code, name=name, category=category,
                          icon=ICONS.get(code), sort_order=order)
            session.add(sport)
            await session.flush()
            created += 1
        else:
            sport.name, sport.category, sport.sort_order = name, category, order
            sport.icon = ICONS.get(code)
        wanted = disciplines or [(code, name)]
        have = {
            d.code
            for d in (
                await session.execute(select(Discipline).where(Discipline.sport_id == sport.id))
            ).scalars()
        }
        for dcode, dname in wanted:
            if dcode not in have:
                session.add(Discipline(sport_id=sport.id, code=dcode, name=dname))
    return created


async def seed_games(session: AsyncSession) -> int:
    """The platform's own mini-games and achievements (reference tier)."""
    existing = {g.code: g for g in (await session.execute(select(Game))).scalars()}
    sports = {s.code: s for s in (await session.execute(select(Sport))).scalars()}
    created = 0
    for code, name, tagline, engine, sport_code, config, direction, unit, order in GAMES:
        game = existing.get(code)
        if game is None:
            session.add(Game(
                code=code, name=name, tagline=tagline, engine=engine,
                sport_id=sports[sport_code].id if sport_code else None,
                config=config, score_direction=direction, score_unit=unit,
                sort_order=order,
            ))
            created += 1
        else:
            game.name, game.tagline, game.engine = name, tagline, engine
            game.config, game.score_direction = config, direction
            game.score_unit, game.sort_order = unit, order
    await session.flush()

    games = {g.code: g for g in (await session.execute(select(Game))).scalars()}
    have = {a.code for a in (await session.execute(select(Achievement))).scalars()}
    for code, name, description, trigger, threshold, game_code in ACHIEVEMENTS:
        if code in have:
            continue
        session.add(Achievement(
            code=code, name=name, description=description, trigger=trigger,
            threshold=threshold,
            game_id=games[game_code].id if game_code else None,
        ))
    return created


async def seed_reference(session: AsyncSession) -> None:
    countries = {c.iso3: c for c in (await session.execute(select(Country))).scalars()}
    for iso3, iso2, name, flag in COUNTRIES:
        if iso3 not in countries:
            country = Country(iso3=iso3, iso2=iso2, name=name, flag_emoji=flag)
            session.add(country)
            countries[iso3] = country
    await session.flush()

    sports = {s.code: s for s in (await session.execute(select(Sport))).scalars()}
    comps = {c.slug: c for c in (await session.execute(select(Competition))).scalars()}
    for slug, name, level, sport_code, editions in COMPETITIONS:
        comp = comps.get(slug)
        if comp is None:
            comp = Competition(
                slug=slug, name=name, level=level,
                sport_id=sports[sport_code].id if sport_code else None,
            )
            session.add(comp)
            await session.flush()
        have = {
            e.label
            for e in (
                await session.execute(
                    select(CompetitionEdition).where(CompetitionEdition.competition_id == comp.id)
                )
            ).scalars()
        }
        for label, year, start, end, city, host_iso3, status in editions:
            if label not in have:
                session.add(CompetitionEdition(
                    competition_id=comp.id, label=label, year=year,
                    start_date=start, end_date=end, host_city=city,
                    host_country_id=countries[host_iso3].id if host_iso3 else None,
                    status=status,
                ))


async def seed_dev_fixtures(session: AsyncSession) -> int:
    sports = {s.code: s for s in (await session.execute(select(Sport))).scalars()}
    disciplines = {
        d.code: d for d in (await session.execute(select(Discipline))).scalars()
    }
    countries = {c.iso3: c for c in (await session.execute(select(Country))).scalars()}
    existing = {
        a.slug for a in (await session.execute(select(Athlete))).scalars()
    }
    created = 0
    for slug, given, family, iso3, dob, sex, sport_code, dcodes in DEV_ATHLETES:
        if slug in existing:
            continue
        athlete = Athlete(
            slug=slug, given_name=given, family_name=family,
            country_id=countries[iso3].id, date_of_birth=dob, sex=sex,
            bio=f"{DEV_BIO_PREFIX}{given} {family} competes in {sports[sport_code].name}.",
        )
        # Default discipline mirrors the sport code when none listed.
        for dcode in dcodes or [sport_code]:
            athlete.disciplines.append(disciplines[dcode])
        session.add(athlete)
        created += 1
    await session.flush()
    await seed_dev_events(session)
    await seed_dev_competitive(session)
    return created


async def seed_dev_competitive(session: AsyncSession) -> None:
    """Fictional records, medals and rankings. Idempotent by natural key."""
    disciplines = {d.code: d for d in (await session.execute(select(Discipline))).scalars()}
    athletes = {a.slug: a for a in (await session.execute(select(Athlete))).scalars()}
    countries = {c.iso3: c for c in (await session.execute(select(Country))).scalars()}
    editions = {
        (e.competition.slug, e.label): e
        for e in (
            await session.execute(
                select(CompetitionEdition).options(
                    selectinload(CompetitionEdition.competition)
                )
            )
        ).scalars()
    }

    have_records = {
        (r.kind, r.discipline_id, r.event_name, r.gender)
        for r in (await session.execute(select(Record))).scalars()
    }
    for (kind, dcode, event_name, gender, slug, iso3, value_kind, num, text,
         unit, iso_date, location) in DEV_RECORDS:
        discipline = disciplines[dcode]
        if (kind, discipline.id, event_name, gender) in have_records:
            continue
        session.add(Record(
            kind=kind, discipline_id=discipline.id, event_name=event_name,
            gender=gender,
            athlete_id=athletes[slug].id if slug else None,
            country_id=countries[iso3].id if iso3 else None,
            value_kind=value_kind, value_num=num, value_text=text, unit=unit,
            set_on=date.fromisoformat(iso_date) if iso_date else None,
            location=location,
        ))

    have_medals = {
        (m.edition_id, m.event_name, m.metal, m.athlete_id)
        for m in (await session.execute(select(Medal))).scalars()
    }
    for (label, comp_slug, dcode, event_name, metal, slug, iso3) in DEV_MEDALS:
        edition = editions[(comp_slug, label)]
        athlete_id = athletes[slug].id if slug else None
        if (edition.id, event_name, metal, athlete_id) in have_medals:
            continue
        session.add(Medal(
            edition_id=edition.id, discipline_id=disciplines[dcode].id,
            event_name=event_name, metal=metal, athlete_id=athlete_id,
            country_id=countries[iso3].id,
        ))

    as_of = date.fromisoformat(RANKING_AS_OF)
    have_rankings = {
        (r.scope, r.methodology, r.discipline_id, r.entity_id, r.as_of)
        for r in (await session.execute(select(Ranking))).scalars()
    }
    for methodology, dcode, scope, entries in DEV_RANKINGS:
        discipline = disciplines[dcode] if dcode else None
        for key, rank, points in entries:
            entity = athletes[key] if scope == "athlete" else countries[key]
            signature = (
                scope, methodology, discipline.id if discipline else None,
                entity.id, as_of,
            )
            if signature in have_rankings:
                continue
            session.add(Ranking(
                scope=scope, methodology=methodology,
                discipline_id=discipline.id if discipline else None,
                sport_id=discipline.sport_id if discipline else None,
                entity_id=entity.id, rank=rank, points=points, as_of=as_of,
            ))


async def seed_dev_events(session: AsyncSession) -> int:
    """Fictional events/results for the fictional athletes. Idempotent by name."""
    disciplines = {d.code: d for d in (await session.execute(select(Discipline))).scalars()}
    athletes = {a.slug: a for a in (await session.execute(select(Athlete))).scalars()}
    editions = {
        (e.competition.slug, e.label): e
        for e in (
            await session.execute(
                select(CompetitionEdition).options(
                    selectinload(CompetitionEdition.competition)
                )
            )
        ).scalars()
    }
    existing = {
        (e.edition_id, e.name) for e in (await session.execute(select(Event))).scalars()
    }
    created = 0
    for (comp_slug, label, dcode, name, gender, phase, start_iso, status,
         entries) in DEV_EVENTS:
        edition = editions[(comp_slug, label)]
        if (edition.id, name) in existing:
            continue
        event = Event(
            edition_id=edition.id,
            discipline_id=disciplines[dcode].id,
            name=name,
            gender=gender,
            phase=phase,
            scheduled_start=datetime.fromisoformat(start_iso) if start_iso else None,
            status=status,
        )
        session.add(event)
        await session.flush()
        for slug, lane, position, result_status, kind, num, text in entries:
            participation = Participation(
                event_id=event.id, athlete_id=athletes[slug].id, lane=lane
            )
            session.add(participation)
            await session.flush()
            if result_status is not None:
                session.add(Result(
                    participation_id=participation.id,
                    position=position,
                    status=result_status,
                    value_kind=kind or "time",
                    value_num=num,
                    value_text=text,
                ))
        created += 1
    return created


async def main(with_fixtures: bool) -> None:
    settings = get_settings()
    async with get_session_factory()() as session:
        new_sports = await seed_taxonomy(session)
        await seed_reference(session)
        new_games = await seed_games(session)
        print(f"Games: {new_games} new mini-games (idempotent).")
        print(f"Taxonomy: {new_sports} new sports (idempotent). Reference catalogue seeded.")
        if with_fixtures:
            if not settings.enable_dev_fixtures:
                print("Refusing dev fixtures: SPORTS_ENABLE_DEV_FIXTURES is not true.")
                sys.exit(2)
            created = await seed_dev_fixtures(session)
            print(f"Dev fixtures: {created} fictional athletes created.")
        await session.commit()
    print("Seed complete.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", action="store_true",
                        help="also seed fictional development athletes (dev-gated)")
    ns = parser.parse_args()
    asyncio.run(main(ns.fixtures))
