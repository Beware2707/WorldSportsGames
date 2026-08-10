"""Game scoring, XP, streaks and achievement unlocking.

Score direction is per-game: a reaction game is ``lower_better`` (milliseconds)
while an accuracy game is ``higher_better`` (hits). Nothing here assumes a
single direction — leaderboards and personal bests both consult the game.
"""

from datetime import date, timedelta

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import (
    Achievement,
    Game,
    GameSession,
    UserAchievement,
    UserProgress,
)

# Levels grow super-linearly so early levels feel quick and later ones earn.
_XP_PER_LEVEL_BASE = 100


def level_for_xp(total_xp: int) -> int:
    """Level 1 at 0 XP; each level costs 100 XP more than the last."""
    level, spent, step = 1, 0, _XP_PER_LEVEL_BASE
    while total_xp >= spent + step:
        spent += step
        step += _XP_PER_LEVEL_BASE
        level += 1
    return level


def xp_progress(total_xp: int) -> tuple[int, int, int]:
    """(level, xp_into_level, xp_needed_for_next)."""
    level, spent, step = 1, 0, _XP_PER_LEVEL_BASE
    while total_xp >= spent + step:
        spent += step
        step += _XP_PER_LEVEL_BASE
        level += 1
    return level, total_xp - spent, step


def xp_for_session(game: Game, score: float, previous_best: float | None) -> int:
    """XP for one play.

    Every completed play earns a floor, so a bad run is never worthless;
    beating a personal best earns a bonus. Deliberately not proportional to
    raw score — scores are not comparable across games.
    """
    xp = 10
    if previous_best is None:
        xp += 15  # first play of this game
    elif is_better(game, score, previous_best):
        xp += 25
    return xp


def is_better(game: Game, candidate: float, incumbent: float) -> bool:
    if game.score_direction == "lower_better":
        return candidate < incumbent
    return candidate > incumbent


async def personal_best(
    session: AsyncSession, user_id: int, game: Game
) -> float | None:
    aggregate = func.min if game.score_direction == "lower_better" else func.max
    return (
        await session.execute(
            select(aggregate(GameSession.score)).where(
                GameSession.user_id == user_id, GameSession.game_id == game.id
            )
        )
    ).scalar_one_or_none()


async def get_or_create_progress(
    session: AsyncSession, user_id: int
) -> UserProgress:
    progress = await session.get(UserProgress, user_id)
    if progress is None:
        progress = UserProgress(user_id=user_id)
        session.add(progress)
        await session.flush()
    return progress


def next_streak(previous_day: date | None, today: date, current: int) -> int:
    """Consecutive-day streak. Same day keeps it; a gap resets it to 1."""
    if previous_day is None:
        return 1
    if previous_day == today:
        return max(current, 1)
    if previous_day == today - timedelta(days=1):
        return current + 1
    return 1


async def unlock_achievements(
    session: AsyncSession,
    user_id: int,
    progress: UserProgress,
    game: Game,
    score: float,
) -> list[Achievement]:
    """Award any achievement whose threshold is now met. Idempotent."""
    already = {
        row[0]
        for row in (
            await session.execute(
                select(UserAchievement.achievement_id).where(
                    UserAchievement.user_id == user_id
                )
            )
        ).all()
    }
    candidates = (
        (await session.execute(select(Achievement))).scalars().all()
    )

    unlocked: list[Achievement] = []
    for achievement in candidates:
        if achievement.id in already:
            continue
        if achievement.game_id is not None and achievement.game_id != game.id:
            continue

        met = False
        match achievement.trigger:
            case "total_xp":
                met = progress.total_xp >= achievement.threshold
            case "game_sessions":
                met = progress.sessions_played >= achievement.threshold
            case "streak_days":
                met = progress.streak_days >= achievement.threshold
            case "game_score":
                # Direction-aware: "under 200ms" and "over 90 points" are both
                # expressible without a second trigger type.
                met = (
                    score <= achievement.threshold
                    if game.score_direction == "lower_better"
                    else score >= achievement.threshold
                )
        if met:
            session.add(
                UserAchievement(user_id=user_id, achievement_id=achievement.id)
            )
            unlocked.append(achievement)
    return unlocked


async def record_session(
    session: AsyncSession,
    user_id: int,
    game: Game,
    score: float,
    detail: dict,
    today: date,
) -> tuple[GameSession, list[Achievement], bool]:
    """Persist a play and update progression.

    Returns (session_row, newly_unlocked, is_personal_best).
    """
    previous = await personal_best(session, user_id, game)
    improved = previous is None or is_better(game, score, previous)
    xp = xp_for_session(game, score, previous)

    row = GameSession(
        game_id=game.id,
        user_id=user_id,
        score=score,
        xp_awarded=xp,
        detail=detail,
        played_on=today,
    )
    session.add(row)

    progress = await get_or_create_progress(session, user_id)
    progress.streak_days = next_streak(progress.last_played_on, today, progress.streak_days)
    progress.total_xp += xp
    progress.sessions_played += 1
    progress.last_played_on = today
    await session.flush()

    unlocked = await unlock_achievements(session, user_id, progress, game, score)
    return row, unlocked, improved
