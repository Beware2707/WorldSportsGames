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
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.config import get_settings
from app.db.session import get_session_factory
from app.models import (
    Athlete,
    Competition,
    CompetitionEdition,
    Country,
    Discipline,
    Event,
    Participation,
    Result,
    Sport,
)
from app.seed.fixtures import (
    COMPETITIONS,
    COUNTRIES,
    DEV_ATHLETES,
    DEV_BIO_PREFIX,
    DEV_EVENTS,
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
    return created


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
