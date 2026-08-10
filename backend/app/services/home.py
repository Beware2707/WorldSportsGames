from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Athlete
from app.repositories.competition import featured_competitions, upcoming_editions
from app.repositories.live import get_live_events, last_seq_map
from app.schemas.athlete import AthleteOut
from app.schemas.competition import CompetitionOut, EditionOut
from app.schemas.event import EventOut
from app.schemas.home import HomeFeed, HomeSection


async def build_home_feed(session: AsyncSession) -> HomeFeed:
    """Compose the home feed server-side.

    Sections render generically on the client by ``kind``.

    ``live_now`` is sourced from LiveEvent rows — the same single source of
    truth the Live Center uses — so the home LIVE indicator can never be
    driven by an edition merely being in progress. No LiveEvent, no LIVE.
    "Spotlight" is used instead of "Trending" until real engagement analytics
    exist (honesty over flash).
    """
    live = await get_live_events(session)
    seqs = await last_seq_map(session, [c.id for c in live])
    upcoming = await upcoming_editions(session)
    featured = await featured_competitions(session)
    spotlight = (
        (
            await session.execute(
                select(Athlete)
                .where(Athlete.is_active.is_(True))
                .options(selectinload(Athlete.country))
                .order_by(Athlete.id.desc())
                .limit(8)
            )
        )
        .scalars()
        .all()
    )

    def edition_item(e) -> dict:
        item = EditionOut.model_validate(e).model_dump(mode="json")
        item["competition"] = CompetitionOut.model_validate(e.competition).model_dump(mode="json")
        return item

    def live_item(coverage) -> dict:
        event = coverage.event
        return {
            "event": EventOut.model_validate(event).model_dump(mode="json"),
            "edition_label": event.edition.label,
            "competition_name": event.edition.competition.name,
            "competition_slug": event.edition.competition.slug,
            "current_phase": coverage.current_phase,
            "last_seq": seqs.get(coverage.id, 0),
        }

    return HomeFeed(
        sections=[
            HomeSection(
                kind="live_now",
                title="Live Now",
                items=[live_item(c) for c in live],
            ),
            HomeSection(
                kind="up_next",
                title="Up Next",
                items=[edition_item(e) for e in upcoming],
            ),
            HomeSection(
                kind="featured_competitions",
                title="Featured Competitions",
                items=[
                    CompetitionOut.model_validate(c).model_dump(mode="json") for c in featured
                ],
            ),
            HomeSection(
                kind="athlete_spotlight",
                title="Athlete Spotlight",
                items=[AthleteOut.model_validate(a).model_dump(mode="json") for a in spotlight],
            ),
        ]
    )
