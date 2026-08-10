from datetime import date
from typing import Annotated

from fastapi import APIRouter, HTTPException, Query, status
from sqlalchemy import select

from app.api.deps import DbSession
from app.models import Discipline
from app.repositories.competitive import (
    country_athletes,
    country_medal_tally,
    country_records,
    editions_with_medals,
    get_country_by_iso3,
    latest_as_of,
    medal_table,
    ranking_entries,
    record_holders,
    records_query,
    resolve_ranked_entities,
)
from app.schemas.athlete import AthleteOut
from app.schemas.catalog import CountryOut, DisciplineOut
from app.schemas.competition import EditionOut
from app.schemas.competitive import (
    CountryProfileOut,
    MedalTableOut,
    MedalTallyOut,
    RankingEntryOut,
    RankingOut,
    RecordOut,
)

router = APIRouter(tags=["competitive"])


def _record_out(record, holders: dict) -> RecordOut:
    holder = holders.get(record.id)
    return RecordOut(
        id=record.id,
        kind=record.kind,
        event_name=record.event_name,
        gender=record.gender,
        value_text=record.value_text,
        value_kind=record.value_kind,
        unit=record.unit,
        set_on=record.set_on,
        location=record.location,
        discipline=DisciplineOut.model_validate(record.discipline),
        country=CountryOut.model_validate(record.country) if record.country else None,
        holder_name=holder[0] if holder else None,
        holder_slug=holder[1] if holder else None,
    )


@router.get("/rankings", response_model=RankingOut)
async def get_rankings(
    session: DbSession,
    scope: Annotated[str, Query(pattern="^(athlete|team|country)$")] = "athlete",
    methodology: Annotated[
        str, Query(pattern="^(world_ranking|olympic_ranking|season_points|elo|medal_count)$")
    ] = "world_ranking",
    discipline: Annotated[str | None, Query()] = None,
    as_of: Annotated[date | None, Query()] = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> RankingOut:
    """One ladder. Ranking methodology is explicit — no sport is assumed to
    rank like any other, and an unknown ladder returns empty, not an error."""
    discipline_row = None
    if discipline:
        discipline_row = (
            await session.execute(select(Discipline).where(Discipline.code == discipline))
        ).scalar_one_or_none()
        if discipline_row is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Discipline not found"
            )

    discipline_id = discipline_row.id if discipline_row else None
    snapshot = as_of or await latest_as_of(session, scope, methodology, discipline_id)
    entries = await ranking_entries(
        session, scope, methodology, discipline_id, snapshot, limit
    )
    names = await resolve_ranked_entities(
        session, scope, [e.entity_id for e in entries]
    )

    return RankingOut(
        scope=scope,
        methodology=methodology,
        discipline=DisciplineOut.model_validate(discipline_row) if discipline_row else None,
        as_of=snapshot,
        entries=[
            RankingEntryOut(
                rank=e.rank,
                points=e.points,
                entity_id=e.entity_id,
                entity_name=names.get(e.entity_id, (None, None, None))[0],
                entity_subtitle=names.get(e.entity_id, (None, None, None))[1],
                entity_slug=names.get(e.entity_id, (None, None, None))[2],
            )
            for e in entries
        ],
    )


@router.get("/records", response_model=list[RecordOut])
async def list_records(
    session: DbSession,
    kind: Annotated[str | None, Query(pattern="^(WR|OR|CR|AR|NR|PB|SB)$")] = None,
    discipline: Annotated[str | None, Query()] = None,
    gender: Annotated[str | None, Query(pattern="^[MFX]$")] = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 100,
) -> list[RecordOut]:
    rows = list(
        (await session.execute(records_query(kind, discipline, gender).limit(limit)))
        .scalars()
        .unique()
    )
    holders = await record_holders(session, rows)
    return [_record_out(r, holders) for r in rows]


@router.get("/medals", response_model=MedalTableOut)
async def get_medal_table(
    session: DbSession,
    edition_id: Annotated[int | None, Query()] = None,
) -> MedalTableOut:
    """Medal table, gold-first ordered. Omit ``edition_id`` for all-time."""
    label = None
    if edition_id is not None:
        editions = {e.id: e for e in await editions_with_medals(session)}
        if edition_id in editions:
            label = editions[edition_id].label

    rows = await medal_table(session, edition_id)
    return MedalTableOut(
        edition_id=edition_id,
        edition_label=label,
        rows=[
            MedalTallyOut(
                country=CountryOut.model_validate(country),
                gold=g,
                silver=s,
                bronze=b,
                total=g + s + b,
                rank=index + 1,
            )
            for index, (country, g, s, b) in enumerate(rows)
        ],
    )


@router.get("/medals/editions", response_model=list[EditionOut])
async def medal_editions(session: DbSession) -> list[EditionOut]:
    """Editions that actually have medal data — so the client's filter never
    offers an option that yields an empty table."""
    return [EditionOut.model_validate(e) for e in await editions_with_medals(session)]


@router.get("/countries/{iso3}", response_model=CountryProfileOut)
async def country_profile(iso3: str, session: DbSession) -> CountryProfileOut:
    country = await get_country_by_iso3(session, iso3)
    if country is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Country not found"
        )

    gold, silver, bronze = await country_medal_tally(session, country.id)
    athletes = await country_athletes(session, country.id)
    records = await country_records(session, country.id)
    holders = await record_holders(session, records)

    return CountryProfileOut(
        country=CountryOut.model_validate(country),
        athlete_count=len(athletes),
        medals=MedalTallyOut(
            country=CountryOut.model_validate(country),
            gold=gold,
            silver=silver,
            bronze=bronze,
            total=gold + silver + bronze,
            rank=0,  # rank is table-relative; meaningless on a single profile
        )
        if (gold or silver or bronze)
        else None,
        athletes=[
            AthleteOut.model_validate(a).model_dump(mode="json") for a in athletes
        ],
        records=[_record_out(r, holders) for r in records],
    )
