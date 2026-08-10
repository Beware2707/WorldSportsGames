"""Game catalogue and leaderboard reads."""

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import (
    Achievement,
    AppUser,
    Country,
    Favorite,
    Game,
    GameSession,
    UserAchievement,
    UserProgress,
)


async def list_games(session: AsyncSession) -> list[Game]:
    rows = await session.execute(
        select(Game)
        .where(Game.is_active.is_(True))
        .options(selectinload(Game.sport))
        .order_by(Game.sort_order, Game.name)
    )
    return list(rows.scalars().all())


async def get_game(session: AsyncSession, code: str) -> Game | None:
    return (
        await session.execute(
            select(Game).where(Game.code == code).options(selectinload(Game.sport))
        )
    ).scalar_one_or_none()


async def leaderboard(
    session: AsyncSession,
    game: Game,
    limit: int = 25,
    user_ids: list[int] | None = None,
) -> list[tuple[int, str, float]]:
    """Best score per user for one game: (user_id, display_name, score).

    One row per player — a leaderboard listing the same person five times for
    five good runs would be useless. ``user_ids`` restricts the pool (friends
    or a single country).
    """
    aggregate = func.min if game.score_direction == "lower_better" else func.max
    best = aggregate(GameSession.score).label("best")

    query = (
        select(GameSession.user_id, AppUser.display_name, best)
        .join(AppUser, AppUser.id == GameSession.user_id)
        .where(GameSession.game_id == game.id)
        .group_by(GameSession.user_id, AppUser.display_name)
        .order_by(best.asc() if game.score_direction == "lower_better" else best.desc())
        .limit(limit)
    )
    if user_ids is not None:
        if not user_ids:
            return []
        query = query.where(GameSession.user_id.in_(user_ids))

    return [(row[0], row[1], row[2]) for row in (await session.execute(query)).all()]


async def users_following_country(
    session: AsyncSession, country_id: int
) -> list[int]:
    """Users who follow a given country — the basis of the country board.

    The platform has no user-nationality field, so "country leaderboard" means
    players who follow that country. Naming it anything else would imply data
    we do not collect.
    """
    rows = await session.execute(
        select(Favorite.user_id).where(
            Favorite.entity_type == "country", Favorite.entity_id == country_id
        )
    )
    return [row[0] for row in rows.all()]


async def get_country_by_iso3(session: AsyncSession, iso3: str) -> Country | None:
    return (
        await session.execute(select(Country).where(Country.iso3 == iso3.upper()))
    ).scalar_one_or_none()


async def user_progress(session: AsyncSession, user_id: int) -> UserProgress | None:
    return await session.get(UserProgress, user_id)


async def user_achievements(
    session: AsyncSession, user_id: int
) -> list[UserAchievement]:
    rows = await session.execute(
        select(UserAchievement)
        .where(UserAchievement.user_id == user_id)
        .options(selectinload(UserAchievement.achievement))
        .order_by(UserAchievement.unlocked_at.desc())
    )
    return list(rows.scalars().all())


async def all_achievements(session: AsyncSession) -> list[Achievement]:
    rows = await session.execute(select(Achievement).order_by(Achievement.threshold))
    return list(rows.scalars().all())


async def user_best_scores(
    session: AsyncSession, user_id: int
) -> dict[int, float]:
    """Personal best per game for one user, in a single query.

    Direction differs per game, so both aggregates are computed and the right
    one is chosen per row rather than assuming higher-is-better.
    """
    rows = await session.execute(
        select(
            GameSession.game_id,
            func.min(GameSession.score),
            func.max(GameSession.score),
            Game.score_direction,
        )
        .join(Game, Game.id == GameSession.game_id)
        .where(GameSession.user_id == user_id)
        .group_by(GameSession.game_id, Game.score_direction)
    )
    return {
        row[0]: (row[1] if row[3] == "lower_better" else row[2])
        for row in rows.all()
    }
