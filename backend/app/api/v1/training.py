from datetime import UTC, datetime, timedelta

from fastapi import APIRouter, HTTPException, status
from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError

from app.api.deps import CurrentUser, DbSession
from app.models import CareerAthlete, GameResult, TrainingSession
from app.schemas.training import (
    CareerRecordOut,
    CareerStatsOut,
    DrillOut,
    TrainingIn,
    TrainingOut,
)
from app.services.career import EVENTS, format_value, get_athlete_for_user
from app.services.training import (
    DAILY_ATTRIBUTE_CAP,
    DRILLS,
    TrainingSubmission,
    gained_in_last_day,
    record_session,
)

router = APIRouter(prefix="/career", tags=["career"])


@router.get("/drills", response_model=list[DrillOut])
async def drill_catalogue() -> list[DrillOut]:
    """Drills the server can validate. A client should refuse to run one that
    is not in this list, because the server will refuse to reward it."""
    return [
        DrillOut(
            code=d.code,
            name=d.name,
            attribute=d.attribute,
            metric_unit=d.metric_unit,
            lower_is_better=d.lower_is_better,
            best_metric=d.best_metric,
            worst_metric=d.worst_metric,
        )
        for d in DRILLS.values()
    ]


@router.post("/training", response_model=TrainingOut, status_code=201)
async def submit_training(
    body: TrainingIn, session: DbSession, user: CurrentUser
) -> TrainingOut:
    """Submit a completed drill.

    The client reports the performance; the SERVER decides the gain. Every
    session is stored, accepted or not, because a silently discarded session
    is indistinguishable from a bug.
    """
    athlete = await get_athlete_for_user(session, user.id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Create a career athlete first"
        )
    drill = DRILLS.get(body.drill)
    if drill is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail=f"Unknown drill {body.drill!r}"
        )

    # Idempotent replay, same contract as result submission.
    if body.client_ref is not None:
        existing = (
            await session.execute(
                select(TrainingSession).where(
                    TrainingSession.career_athlete_id == athlete.id,
                    TrainingSession.client_ref == body.client_ref,
                )
            )
        ).scalar_one_or_none()
        if existing is not None:
            return await _training_out(session, existing, athlete)

    row = await record_session(
        session,
        athlete,
        TrainingSubmission(drill=drill, metric=body.metric, client_ref=body.client_ref),
    )
    try:
        await session.commit()
    except IntegrityError:
        await session.rollback()
        athlete = await get_athlete_for_user(session, user.id)
        existing = (
            await session.execute(
                select(TrainingSession).where(
                    TrainingSession.career_athlete_id == athlete.id,
                    TrainingSession.client_ref == body.client_ref,
                )
            )
        ).scalar_one_or_none()
        if existing is None:
            raise
        return await _training_out(session, existing, athlete)

    athlete = await get_athlete_for_user(session, user.id)
    return await _training_out(session, row, athlete)


async def _training_out(
    session, row: TrainingSession, athlete: CareerAthlete
) -> TrainingOut:
    current = next(
        (a.value for a in athlete.attributes if a.key == row.attribute_key), 0.0
    )
    used = await gained_in_last_day(session, athlete.id, row.attribute_key)
    return TrainingOut(
        accepted=row.accepted,
        rejection_reason=row.rejection_reason,
        drill=row.drill,
        attribute=row.attribute_key,
        quality=row.quality,
        attribute_before=row.attribute_before,
        attribute_after=current,
        attribute_gain=row.attribute_gain,
        xp_awarded=row.xp_awarded,
        total_xp=athlete.total_xp,
        career_stage=athlete.career_stage,
        daily_remaining=max(0.0, DAILY_ATTRIBUTE_CAP - used),
    )


@router.get("/records", response_model=list[CareerRecordOut])
async def career_records(session: DbSession, user: CurrentUser) -> list[CareerRecordOut]:
    """Personal best, season best and the best mark anyone has run.

    Derived from validated results rather than stored separately: a record
    table that can disagree with the results it summarises is a record table
    that will.
    """
    athlete = await get_athlete_for_user(session, user.id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Create a career athlete first"
        )

    season_start = datetime.now(UTC) - timedelta(days=365)

    async def best_value(event_code: str, lower_is_better: bool, *conditions):
        """Best VALID mark for an event. Taking a parameter rather than
        closing over the loop variable keeps each query bound to the event
        it was written for."""
        aggregate = func.min if lower_is_better else func.max
        return (
            await session.execute(
                select(aggregate(GameResult.value_num)).where(
                    GameResult.event_code == event_code,
                    GameResult.is_valid.is_(True),
                    *conditions,
                )
            )
        ).scalar_one_or_none()

    out: list[CareerRecordOut] = []
    for event in EVENTS.values():
        personal = await best_value(
            event.code, event.lower_is_better,
            GameResult.career_athlete_id == athlete.id,
        )
        seasonal = await best_value(
            event.code, event.lower_is_better,
            GameResult.career_athlete_id == athlete.id,
            GameResult.created_at >= season_start,
        )
        world = await best_value(event.code, event.lower_is_better)

        holder = None
        if world is not None:
            holder = (
                await session.execute(
                    select(CareerAthlete.name)
                    .join(GameResult, GameResult.career_athlete_id == CareerAthlete.id)
                    .where(
                        GameResult.event_code == event.code,
                        GameResult.is_valid.is_(True),
                        GameResult.value_num == world,
                    )
                    .limit(1)
                )
            ).scalar_one_or_none()

        out.append(
            CareerRecordOut(
                event=event.code,
                unit=event.unit,
                lower_is_better=event.lower_is_better,
                personal_best=personal,
                personal_best_text=format_value(event, personal) if personal else None,
                season_best=seasonal,
                season_best_text=format_value(event, seasonal) if seasonal else None,
                world_best=world,
                world_best_text=format_value(event, world) if world else None,
                world_best_holder=holder,
            )
        )
    return out


@router.get("/statistics", response_model=CareerStatsOut)
async def career_statistics(session: DbSession, user: CurrentUser) -> CareerStatsOut:
    """Career totals, computed from the audit trail.

    Rejected races are reported, not hidden: a player who cannot see that a
    run was refused cannot tell a false start from a bug.
    """
    athlete = await get_athlete_for_user(session, user.id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Create a career athlete first"
        )

    totals = (
        await session.execute(
            select(
                func.count(GameResult.id),
                func.count(GameResult.id).filter(GameResult.is_valid.is_(True)),
                func.min(GameResult.reaction_ms).filter(GameResult.is_valid.is_(True)),
                func.avg(GameResult.reaction_ms).filter(GameResult.is_valid.is_(True)),
            ).where(GameResult.career_athlete_id == athlete.id)
        )
    ).one()
    races, valid, best_reaction, avg_reaction = totals

    training_count = (
        await session.execute(
            select(func.count(TrainingSession.id)).where(
                TrainingSession.career_athlete_id == athlete.id,
                TrainingSession.accepted.is_(True),
            )
        )
    ).scalar_one()

    # Distance covered is the sum of each valid result's event distance.
    distance = 0.0
    for event in EVENTS.values():
        count = (
            await session.execute(
                select(func.count(GameResult.id)).where(
                    GameResult.career_athlete_id == athlete.id,
                    GameResult.event_code == event.code,
                    GameResult.is_valid.is_(True),
                )
            )
        ).scalar_one()
        distance += count * event.distance_m

    return CareerStatsOut(
        races=races or 0,
        valid_races=valid or 0,
        rejected_races=(races or 0) - (valid or 0),
        # Head-to-head wins live in the tournament/race layer; a career "win"
        # is not inferable from a solo time, so it is reported as 0 rather
        # than guessed at.
        wins=0,
        training_sessions=training_count or 0,
        best_reaction_ms=float(best_reaction) if best_reaction is not None else None,
        average_reaction_ms=float(avg_reaction) if avg_reaction is not None else None,
        total_distance_m=distance,
        total_xp=athlete.total_xp,
        career_stage=athlete.career_stage,
    )
