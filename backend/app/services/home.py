from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import Athlete
from app.repositories.competition import featured_competitions, live_editions, upcoming_editions
from app.schemas.athlete import AthleteOut
from app.schemas.competition import CompetitionOut, EditionOut
from app.schemas.home import HomeFeed, HomeSection


async def build_home_feed(session: AsyncSession) -> HomeFeed:
    """Compose the home feed server-side.

    Sections render generically on the client by ``kind``. ``live_now`` only
    ever contains editions whose status is genuinely ``live`` in the database —
    there is no simulated live data path. "Spotlight" is used instead of
    "Trending" until real engagement analytics exist (honesty over flash).
    """
    live = await live_editions(session)
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

    return HomeFeed(
        sections=[
            HomeSection(
                kind="live_now",
                title="Live Now",
                items=[edition_item(e) for e in live],
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
