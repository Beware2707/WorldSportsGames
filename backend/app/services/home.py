from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import (
    Athlete,
    Competition,
    CompetitionEdition,
    Discipline,
    Sport,
    athlete_discipline,
)
from app.repositories.competition import featured_competitions, upcoming_editions
from app.repositories.live import get_live_events, last_seq_map
from app.repositories.personalization import favorite_ids
from app.schemas.athlete import AthleteOut
from app.schemas.catalog import SportOut
from app.schemas.competition import CompetitionOut, EditionOut
from app.schemas.event import EventOut
from app.schemas.home import HomeFeed, HomeSection

_FOLLOW_SECTION_LIMIT = 12


async def build_home_feed(session: AsyncSession, user_id: int | None = None) -> HomeFeed:
    """Compose the home feed server-side.

    Sections render generically on the client by ``kind``, so personalization
    changes composition without an app release. Signed-in users with follows
    get "Your …" sections inserted after Live Now; anonymous users get the
    generic feed and nothing breaks.

    ``live_now`` is sourced from LiveEvent rows — the same single source of
    truth the Live Center uses — so the home LIVE indicator can never be
    driven by an edition merely being in progress. No LiveEvent, no LIVE.
    """
    sections: list[HomeSection] = [await _live_section(session)]

    if user_id is not None:
        sections.extend(await _followed_sections(session, user_id))

    upcoming = await upcoming_editions(session)
    featured = await featured_competitions(session)

    sections.append(
        HomeSection(
            kind="up_next",
            title="Up Next",
            items=[_edition_item(e) for e in upcoming],
        )
    )
    sections.append(
        HomeSection(
            kind="featured_competitions",
            title="Featured Competitions",
            items=[CompetitionOut.model_validate(c).model_dump(mode="json") for c in featured],
        )
    )
    sections.append(
        HomeSection(
            kind="athlete_spotlight",
            title="Athlete Spotlight",
            items=[
                AthleteOut.model_validate(a).model_dump(mode="json")
                for a in await _spotlight_athletes(session)
            ],
        )
    )
    return HomeFeed(sections=sections)


async def _live_section(session: AsyncSession) -> HomeSection:
    coverage = await get_live_events(session)
    seqs = await last_seq_map(session, [c.id for c in coverage])
    return HomeSection(
        kind="live_now",
        title="Live Now",
        items=[
            {
                "event": EventOut.model_validate(c.event).model_dump(mode="json"),
                "edition_label": c.event.edition.label,
                "competition_name": c.event.edition.competition.name,
                "competition_slug": c.event.edition.competition.slug,
                "current_phase": c.current_phase,
                "last_seq": seqs.get(c.id, 0),
            }
            for c in coverage
        ],
    )


async def _followed_sections(session: AsyncSession, user_id: int) -> list[HomeSection]:
    """Sections derived from the user's follows. Empty ones are omitted."""
    athlete_ids = await favorite_ids(session, user_id, "athlete")
    sport_ids = await favorite_ids(session, user_id, "sport")
    country_ids = await favorite_ids(session, user_id, "country")
    competition_ids = await favorite_ids(session, user_id, "competition")

    sections: list[HomeSection] = []

    # Followed athletes, plus athletes representing followed countries.
    if athlete_ids or country_ids:
        conditions = []
        if athlete_ids:
            conditions.append(Athlete.id.in_(athlete_ids))
        if country_ids:
            conditions.append(Athlete.country_id.in_(country_ids))
        rows = (
            (
                await session.execute(
                    select(Athlete)
                    .where(or_(*conditions))
                    .options(selectinload(Athlete.country))
                    .order_by(Athlete.family_name)
                    .limit(_FOLLOW_SECTION_LIMIT)
                )
            )
            .scalars()
            .unique()
        )
        items = [AthleteOut.model_validate(a).model_dump(mode="json") for a in rows]
        if items:
            sections.append(
                HomeSection(kind="your_athletes", title="Your Athletes", items=items)
            )

    if sport_ids:
        rows = (
            (
                await session.execute(
                    select(Sport)
                    .where(Sport.id.in_(sport_ids))
                    .order_by(Sport.sort_order)
                    .limit(_FOLLOW_SECTION_LIMIT)
                )
            )
            .scalars()
            .all()
        )
        items = [SportOut.model_validate(s).model_dump(mode="json") for s in rows]
        if items:
            sections.append(
                HomeSection(kind="your_sports", title="Your Sports", items=items)
            )

    # Upcoming editions of followed competitions, or of followed sports.
    if competition_ids or sport_ids:
        query = (
            select(CompetitionEdition)
            .join(Competition, CompetitionEdition.competition_id == Competition.id)
            .where(CompetitionEdition.status.in_(["upcoming", "in_progress"]))
            .options(
                selectinload(CompetitionEdition.competition).selectinload(
                    Competition.sport
                ),
                selectinload(CompetitionEdition.host_country),
            )
            .order_by(CompetitionEdition.start_date.asc().nulls_last())
            .limit(_FOLLOW_SECTION_LIMIT)
        )
        conditions = []
        if competition_ids:
            conditions.append(Competition.id.in_(competition_ids))
        if sport_ids:
            conditions.append(Competition.sport_id.in_(sport_ids))
        rows = (await session.execute(query.where(or_(*conditions)))).scalars().unique()
        items = [_edition_item(e) for e in rows]
        if items:
            sections.append(
                HomeSection(
                    kind="following_schedule", title="Your Competitions", items=items
                )
            )

    return sections


async def _spotlight_athletes(session: AsyncSession) -> list[Athlete]:
    rows = await session.execute(
        select(Athlete)
        .where(Athlete.is_active.is_(True))
        .options(selectinload(Athlete.country))
        .order_by(Athlete.id.desc())
        .limit(8)
    )
    return list(rows.scalars().all())


def _edition_item(edition: CompetitionEdition) -> dict:
    item = EditionOut.model_validate(edition).model_dump(mode="json")
    item["competition"] = CompetitionOut.model_validate(
        edition.competition
    ).model_dump(mode="json")
    return item


async def athletes_for_sports(session: AsyncSession, sport_ids: list[int]) -> list[Athlete]:
    """Athletes competing in any discipline of the given sports."""
    if not sport_ids:
        return []
    rows = await session.execute(
        select(Athlete)
        .join(athlete_discipline)
        .join(Discipline, athlete_discipline.c.discipline_id == Discipline.id)
        .where(Discipline.sport_id.in_(sport_ids))
        .options(selectinload(Athlete.country))
        .distinct()
        .limit(_FOLLOW_SECTION_LIMIT)
    )
    return list(rows.scalars().unique().all())
