from fastapi import APIRouter, HTTPException, status
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from app.api.deps import CurrentUser, DbSession
from app.models import CareerTournament
from app.schemas.career import ResultIn
from app.schemas.tournament import (
    RoundOut,
    TournamentOut,
    TournamentResultOut,
    TournamentStartIn,
)
from app.services.career import (
    EVENTS,
    Submission,
    get_athlete_for_user,
    record_result,
)
from app.services.tournament import (
    active_tournament,
    apply_round_result,
    get_tournament,
    open_round,
    start_tournament,
)

router = APIRouter(prefix="/career/tournaments", tags=["tournaments"])


def _tournament_out(tournament: CareerTournament) -> TournamentOut:
    return TournamentOut(
        id=tournament.id,
        event=tournament.event_code,
        current_round=tournament.current_round,
        status=tournament.status,
        final_position=tournament.final_position,
        rounds=[
            RoundOut(
                round=r.round_name,
                field=r.field,
                position=r.position,
                advanced=r.advanced,
            )
            for r in tournament.rounds
        ],
    )


async def _require_athlete(session, user_id: int):
    athlete = await get_athlete_for_user(session, user_id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Create a career athlete first",
        )
    return athlete


@router.post("", response_model=TournamentOut, status_code=201)
async def start(
    body: TournamentStartIn, session: DbSession, user: CurrentUser
) -> TournamentOut:
    """Enter a tournament. The server generates and stores the qualification
    field immediately — opponents are decided before the first race, not after."""
    athlete = await _require_athlete(session, user.id)
    if body.event not in EVENTS:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail=f"Unknown event {body.event!r}"
        )
    if await active_tournament(session, athlete.id, body.event) is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="Finish your current tournament for this event first",
        )

    tournament = await start_tournament(session, athlete, body.event)
    await session.commit()
    tournament = await get_tournament(session, tournament.id, athlete.id)
    return _tournament_out(tournament)


@router.get("", response_model=list[TournamentOut])
async def my_tournaments(session: DbSession, user: CurrentUser) -> list[TournamentOut]:
    athlete = await _require_athlete(session, user.id)
    rows = (
        (
            await session.execute(
                select(CareerTournament)
                .where(CareerTournament.career_athlete_id == athlete.id)
                .options(selectinload(CareerTournament.rounds))
                .order_by(CareerTournament.id.desc())
                .limit(20)
            )
        )
        .scalars()
        .all()
    )
    return [_tournament_out(t) for t in rows]


@router.get("/{tournament_id}", response_model=TournamentOut)
async def tournament_detail(
    tournament_id: int, session: DbSession, user: CurrentUser
) -> TournamentOut:
    athlete = await _require_athlete(session, user.id)
    tournament = await get_tournament(session, tournament_id, athlete.id)
    if tournament is None:
        # Owner-scoped: another player's tournament is indistinguishable from
        # a missing one.
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Tournament not found"
        )
    return _tournament_out(tournament)


@router.post("/{tournament_id}/results", response_model=TournamentResultOut, status_code=201)
async def submit_round_result(
    tournament_id: int, body: ResultIn, session: DbSession, user: CurrentUser
) -> TournamentResultOut:
    """Run the current round.

    The submission passes the same anti-cheat validation as a free run. Only
    a VALIDATED result consumes the round; a rejected one leaves it open so a
    false start can be retried. Position and advancement come from the stored
    field — the client never reports its own placing.
    """
    athlete = await _require_athlete(session, user.id)
    tournament = await get_tournament(session, tournament_id, athlete.id)
    if tournament is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Tournament not found"
        )
    if tournament.status != "in_progress":
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"This tournament is {tournament.status}",
        )
    if body.event != tournament.event_code:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="Result event does not match the tournament event",
        )
    round_row = open_round(tournament)
    if round_row is None:  # pragma: no cover - defensive; state machine bug
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail="No round is open"
        )

    event = EVENTS[tournament.event_code]
    result_row, improved = await record_result(
        session,
        athlete,
        Submission(
            event=event,
            value_num=body.value_num,
            reaction_ms=body.reaction_ms,
            splits=body.splits,
            wind=body.wind,
            rng_seed=body.rng_seed,
            input_digest=body.input_digest,
        ),
    )

    if not result_row.is_valid:
        await session.commit()
        return TournamentResultOut(
            accepted=False,
            rejection_reason=result_row.rejection_reason,
            round=round_row.round_name,
            position=None,
            advanced=None,
            tournament_status=tournament.status,
            final_position=None,
            is_personal_best=False,
            xp_awarded=0,
            total_xp=athlete.total_xp,
        )

    round_row = await apply_round_result(
        session, tournament, round_row, result_row.value_num, result_row.id
    )
    await session.commit()

    return TournamentResultOut(
        accepted=True,
        rejection_reason=None,
        round=round_row.round_name,
        position=round_row.position,
        advanced=round_row.advanced,
        tournament_status=tournament.status,
        final_position=tournament.final_position,
        is_personal_best=improved,
        xp_awarded=result_row.xp_awarded,
        total_xp=athlete.total_xp,
    )
