from datetime import UTC, datetime, timedelta
from typing import Annotated

from fastapi import APIRouter, HTTPException, Query, status
from sqlalchemy import func, select, update
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import selectinload

from app.api.deps import CurrentUser, DbSession, OptionalUser
from app.api.v1.friends import friend_ids
from app.models import CareerAthlete, CareerAttribute, CareerSave, Country, GameResult
from app.schemas.career import (
    CareerAthleteIn,
    CareerAthleteOut,
    CareerLeaderboardOut,
    CareerLeaderboardRowOut,
    EventCatalogueOut,
    ResultIn,
    ResultOut,
    SaveIn,
    SaveOut,
)
from app.schemas.catalog import CountryOut
from app.services.career import (
    EVENTS,
    Submission,
    default_attributes,
    get_athlete_for_user,
    merge_saves,
    record_result,
)

router = APIRouter(prefix="/career", tags=["career"])


def _athlete_out(athlete: CareerAthlete) -> CareerAthleteOut:
    return CareerAthleteOut(
        id=athlete.id,
        name=athlete.name,
        gender=athlete.gender,
        career_stage=athlete.career_stage,
        total_xp=athlete.total_xp,
        country=CountryOut.model_validate(athlete.country) if athlete.country else None,
        appearance=athlete.appearance,
        attributes={a.key: a.value for a in athlete.attributes},
    )


@router.get("/events", response_model=list[EventCatalogueOut])
async def event_catalogue() -> list[EventCatalogueOut]:
    """Events the server can validate. The Unreal client should refuse to
    submit results for events not in this list."""
    return [
        EventCatalogueOut(
            code=e.code,
            name=e.name,
            value_kind=e.value_kind,
            lower_is_better=e.lower_is_better,
            unit=e.unit,
            splits_expected=e.splits_expected,
            requires_reaction=e.requires_reaction,
        )
        for e in EVENTS.values()
    ]


@router.post("/athlete", response_model=CareerAthleteOut, status_code=201)
async def create_athlete(
    body: CareerAthleteIn, session: DbSession, user: CurrentUser
) -> CareerAthleteOut:
    if await get_athlete_for_user(session, user.id) is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="You already have a career athlete",
        )
    if body.country_id is not None and (
        await session.get(Country, body.country_id) is None
    ):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Country not found"
        )

    athlete = CareerAthlete(
        user_id=user.id,
        name=body.name,
        gender=body.gender,
        country_id=body.country_id,
        appearance=body.appearance,
    )
    session.add(athlete)
    await session.flush()
    for key, value in default_attributes().items():
        session.add(
            CareerAttribute(career_athlete_id=athlete.id, key=key, value=value)
        )
    await session.commit()

    athlete = await get_athlete_for_user(session, user.id)
    return _athlete_out(athlete)


@router.get("/athlete", response_model=CareerAthleteOut)
async def my_athlete(session: DbSession, user: CurrentUser) -> CareerAthleteOut:
    athlete = await get_athlete_for_user(session, user.id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Create a career athlete first"
        )
    return _athlete_out(athlete)


@router.post("/results", response_model=ResultOut, status_code=201)
async def submit_result(
    body: ResultIn, session: DbSession, user: CurrentUser
) -> ResultOut:
    """Submit a completed event.

    Always stored; only a validated result earns XP or reaches leaderboards.
    The response is the server's authoritative progression state — the client
    never computes its own reward.
    """
    athlete = await get_athlete_for_user(session, user.id)
    if athlete is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Create a career athlete first"
        )
    event = EVENTS.get(body.event)
    if event is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail=f"Unknown event {body.event!r}"
        )

    row, improved = await record_result(
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
    await session.commit()

    return ResultOut(
        accepted=row.is_valid,
        rejection_reason=row.rejection_reason,
        event=event.code,
        value_text=row.value_text,
        is_personal_best=improved,
        xp_awarded=row.xp_awarded,
        total_xp=athlete.total_xp,
        career_stage=athlete.career_stage,
    )


_PERIODS: dict[str, timedelta | None] = {
    "all_time": None,
    "weekly": timedelta(days=7),
    "monthly": timedelta(days=30),
}


@router.get("/leaderboard", response_model=CareerLeaderboardOut)
async def career_leaderboard(
    session: DbSession,
    user: OptionalUser,
    event: Annotated[str, Query()],
    scope: Annotated[str, Query(pattern="^(global|country|friends)$")] = "global",
    country: Annotated[str | None, Query(min_length=3, max_length=3)] = None,
    period: Annotated[str, Query(pattern="^(all_time|weekly|monthly)$")] = "all_time",
    limit: Annotated[int, Query(ge=1, le=100)] = 25,
) -> CareerLeaderboardOut:
    """Validated results only, one row per athlete, direction-aware.

    The friends scope covers accepted friendships plus the caller — it exists
    now that a real social graph does, and it requires sign-in.
    """
    definition = EVENTS.get(event)
    if definition is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail=f"Unknown event {event!r}"
        )

    aggregate = func.min if definition.lower_is_better else func.max
    best = aggregate(GameResult.value_num).label("best")

    query = (
        select(CareerAthlete, best)
        .join(GameResult, GameResult.career_athlete_id == CareerAthlete.id)
        .where(
            GameResult.event_code == definition.code,
            GameResult.is_valid.is_(True),
        )
        .group_by(CareerAthlete.id)
        .order_by(best.asc() if definition.lower_is_better else best.desc())
        .limit(limit)
        .options(selectinload(CareerAthlete.country))
    )

    window = _PERIODS[period]
    if window is not None:
        query = query.where(GameResult.created_at >= datetime.now(UTC) - window)

    if scope == "country":
        if country is None:
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail="country is required for the country scope",
            )
        query = query.join(Country, CareerAthlete.country_id == Country.id).where(
            Country.iso3 == country.upper()
        )
    elif scope == "friends":
        if user is None:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Sign in to see this leaderboard",
            )
        pool = [*await friend_ids(session, user.id), user.id]
        query = query.where(CareerAthlete.user_id.in_(pool))

    from app.services.career import format_value

    rows = (await session.execute(query)).all()
    return CareerLeaderboardOut(
        event=definition.code,
        scope=scope,
        period=period,
        lower_is_better=definition.lower_is_better,
        unit=definition.unit,
        rows=[
            CareerLeaderboardRowOut(
                rank=index + 1,
                athlete_name=athlete.name,
                country=CountryOut.model_validate(athlete.country)
                if athlete.country
                else None,
                value_num=value,
                value_text=format_value(definition, value),
            )
            for index, (athlete, value) in enumerate(rows)
        ],
    )


@router.get("/save", response_model=SaveOut)
async def get_save(session: DbSession, user: CurrentUser) -> SaveOut:
    save = await session.get(CareerSave, user.id)
    if save is None:
        # A fresh account has an empty save at version 0; the first PUT with
        # base_version=0 creates it.
        return SaveOut(payload={}, version=0, updated_at=None)
    return SaveOut(payload=save.payload, version=save.version, updated_at=save.updated_at)


@router.put("/save", response_model=SaveOut)
async def put_save(body: SaveIn, session: DbSession, user: CurrentUser) -> SaveOut:
    """Optimistic-concurrency write.

    On version mismatch: 409 with the current server state AND an additive
    merge suggestion, so a client can resolve without losing either device's
    monotonic progress. Never last-write-wins — that loses progression
    silently, which is the worst possible failure (§30).
    """
    save = await session.get(CareerSave, user.id)
    current_version = save.version if save else 0

    # The common conflict — base_version doesn't match what's stored — is
    # detected by this read, so no exception is needed for it. The remaining
    # races (two first-writes; two writes from the same version) are caught by
    # DB-level guards below, never by a Python check-then-set that would let
    # the second write silently clobber the first (the §30 loss).
    if body.base_version != current_version:
        return _save_conflict(save, body.payload)

    if save is None:
        save = CareerSave(user_id=user.id, payload=body.payload, version=1)
        session.add(save)
        try:
            await session.commit()
        except IntegrityError:
            # A concurrent first write won the primary key.
            await session.rollback()
            return _save_conflict(await session.get(CareerSave, user.id), body.payload)
        await session.refresh(save)
        return SaveOut(
            payload=save.payload, version=save.version, updated_at=save.updated_at
        )

    # Version-conditional UPDATE with RETURNING: if a concurrent write already
    # bumped the version, zero rows match and this is a conflict. RETURNING
    # gives us the post-update version/timestamp in the same round-trip, so we
    # never re-fetch a stale identity-mapped object after a Core update.
    result = await session.execute(
        update(CareerSave)
        .where(
            CareerSave.user_id == user.id,
            CareerSave.version == body.base_version,
        )
        .values(payload=body.payload, version=CareerSave.version + 1)
        .returning(CareerSave.version, CareerSave.updated_at)
    )
    row = result.first()
    if row is None:
        await session.rollback()
        return _save_conflict(await session.get(CareerSave, user.id), body.payload)
    await session.commit()
    return SaveOut(payload=body.payload, version=row[0], updated_at=row[1])


def _save_conflict(save, client_payload: dict) -> None:
    """Raise a 409 with the server state and an additive merge suggestion."""
    server_payload = save.payload if save else {}
    raise HTTPException(
        status_code=status.HTTP_409_CONFLICT,
        detail={
            "message": "Save conflict: another device wrote first",
            "server": {
                "payload": server_payload,
                "version": save.version if save else 0,
            },
            "suggested_merge": merge_saves(server_payload, client_payload),
        },
    )
