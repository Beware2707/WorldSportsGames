from datetime import UTC, datetime
from typing import Annotated

from fastapi import APIRouter, HTTPException, Query, status

from app.api.deps import CurrentUser, DbSession, OptionalUser
from app.repositories.games import (
    all_achievements,
    get_country_by_iso3,
    get_game,
    leaderboard,
    list_games,
    user_achievements,
    user_best_scores,
    user_progress,
    users_following_country,
)
from app.schemas.games import (
    AchievementOut,
    DailyChallengeOut,
    GameOut,
    LeaderboardOut,
    LeaderboardRowOut,
    ProgressOut,
    SubmitScoreIn,
    SubmitScoreOut,
)
from app.services.games import record_session, xp_progress

router = APIRouter(prefix="/games", tags=["games"])

# Guards against absurd submissions. Scores outside these bounds are rejected
# rather than stored — a leaderboard is only as trustworthy as its worst row.
_SCORE_BOUNDS = {
    "reaction": (50.0, 60_000.0),   # milliseconds
    "accuracy": (0.0, 100_000.0),
    "timing": (0.0, 100_000.0),
    "sequence": (0.0, 100_000.0),
}


@router.get("", response_model=list[GameOut])
async def games_catalogue(session: DbSession, user: OptionalUser) -> list[GameOut]:
    games = await list_games(session)
    bests = await user_best_scores(session, user.id) if user else {}
    out = []
    for game in games:
        item = GameOut.model_validate(game)
        item.personal_best = bests.get(game.id)
        out.append(item)
    return out


@router.get("/daily", response_model=DailyChallengeOut)
async def daily_challenge(session: DbSession, user: OptionalUser) -> DailyChallengeOut:
    """Game of the day, chosen deterministically from the date.

    Same for everyone, no stored schedule, and stable for the whole UTC day.
    """
    games = await list_games(session)
    if not games:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="No games available"
        )
    today = datetime.now(UTC).date()
    game = games[today.toordinal() % len(games)]

    item = GameOut.model_validate(game)
    if user:
        item.personal_best = (await user_best_scores(session, user.id)).get(game.id)
    return DailyChallengeOut(date=today, game=item)


@router.post("/{code}/sessions", response_model=SubmitScoreOut, status_code=201)
async def submit_score(
    code: str, body: SubmitScoreIn, session: DbSession, user: CurrentUser
) -> SubmitScoreOut:
    """Record a completed play.

    XP is computed server-side from the game's own rules — the client submits
    a score, never its own reward.
    """
    game = await get_game(session, code)
    if game is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Game not found")

    low, high = _SCORE_BOUNDS.get(game.engine, (0.0, 1_000_000.0))
    if not (low <= body.score <= high):
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=f"Score must be between {low} and {high} for this game",
        )

    row, unlocked, improved = await record_session(
        session,
        user_id=user.id,
        game=game,
        score=body.score,
        detail=body.detail,
        today=datetime.now(UTC).date(),
    )
    progress = await user_progress(session, user.id)
    await session.commit()

    level, into, needed = xp_progress(progress.total_xp)
    return SubmitScoreOut(
        score=row.score,
        xp_awarded=row.xp_awarded,
        is_personal_best=improved,
        total_xp=progress.total_xp,
        level=level,
        xp_into_level=into,
        xp_for_next_level=needed,
        streak_days=progress.streak_days,
        unlocked=[AchievementOut.model_validate(a) for a in unlocked],
    )


@router.get("/{code}/leaderboard", response_model=LeaderboardOut)
async def game_leaderboard(
    code: str,
    session: DbSession,
    user: OptionalUser,
    scope: Annotated[str, Query(pattern="^(global|country|friends)$")] = "global",
    country: Annotated[str | None, Query(min_length=3, max_length=3)] = None,
    limit: Annotated[int, Query(ge=1, le=100)] = 25,
) -> LeaderboardOut:
    """One row per player, ordered by the game's own score direction.

    ``country`` scope means players who follow that country — the platform
    stores no nationality for users, so calling it anything else would imply
    data that does not exist.
    """
    game = await get_game(session, code)
    if game is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Game not found")

    pool: list[int] | None = None
    label: str | None = None

    if scope == "country":
        if country is None:
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail="country is required for the country scope",
            )
        row = await get_country_by_iso3(session, country)
        if row is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Country not found"
            )
        pool = await users_following_country(session, row.id)
        label = row.name
    elif scope == "friends":
        if user is None:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Sign in to see this leaderboard",
            )
        # There is no social graph yet — following an athlete is not a
        # friendship. Returning an empty pool (and saying so in the client) is
        # honest; inventing one from follows would be a fabricated ranking.
        pool = []
        label = "Friends"

    rows = await leaderboard(session, game, limit=limit, user_ids=pool)
    return LeaderboardOut(
        game_code=game.code,
        scope=scope,
        scope_label=label,
        score_direction=game.score_direction,
        score_unit=game.score_unit,
        rows=[
            LeaderboardRowOut(
                rank=index + 1, user_id=uid, display_name=name, score=score
            )
            for index, (uid, name, score) in enumerate(rows)
        ],
    )


@router.get("/me/progress", response_model=ProgressOut)
async def my_progress(session: DbSession, user: CurrentUser) -> ProgressOut:
    progress = await user_progress(session, user.id)
    unlocked_rows = await user_achievements(session, user.id)
    unlocked_codes = {row.achievement.code for row in unlocked_rows}
    everything = await all_achievements(session)

    total_xp = progress.total_xp if progress else 0
    level, into, needed = xp_progress(total_xp)
    return ProgressOut(
        total_xp=total_xp,
        level=level,
        xp_into_level=into,
        xp_for_next_level=needed,
        sessions_played=progress.sessions_played if progress else 0,
        streak_days=progress.streak_days if progress else 0,
        last_played_on=progress.last_played_on if progress else None,
        achievements=[
            AchievementOut.model_validate(row.achievement) for row in unlocked_rows
        ],
        locked_achievements=[
            AchievementOut.model_validate(a)
            for a in everything
            if a.code not in unlocked_codes
        ],
    )
