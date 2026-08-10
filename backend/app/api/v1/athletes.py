from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, status

from app.api.deps import DbSession
from app.api.v1.competitive import _record_out
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.athlete import athletes_query, get_athlete_by_slug
from app.repositories.competitive import (
    athlete_medals,
    athlete_rankings,
    athlete_recent_results,
    athlete_records,
    record_holders,
)
from app.schemas.athlete import AthleteDetailOut, AthleteOut
from app.schemas.catalog import DisciplineOut
from app.schemas.competitive import AthleteProfileOut

router = APIRouter(tags=["athletes"])


@router.get("/athletes", response_model=Page[AthleteOut])
async def list_athletes(
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    sport: Annotated[str | None, Query()] = None,
    discipline: Annotated[str | None, Query()] = None,
    country: Annotated[str | None, Query(min_length=3, max_length=3)] = None,
    q: Annotated[str | None, Query(min_length=1)] = None,
    sort: Annotated[str | None, Query(pattern="^-?name$")] = None,
) -> Page[AthleteOut]:
    query = athletes_query(sport=sport, discipline=discipline, country=country, q=q, sort=sort)
    items, total, pages = await paginate(session, query, params)
    return Page(
        items=[AthleteOut.model_validate(a) for a in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/athletes/{slug}", response_model=AthleteDetailOut)
async def get_athlete(slug: str, session: DbSession) -> AthleteDetailOut:
    athlete = await get_athlete_by_slug(session, slug)
    if athlete is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Athlete not found")
    return AthleteDetailOut.model_validate(athlete)


@router.get("/athletes/{slug}/profile", response_model=AthleteProfileOut)
async def get_athlete_profile(slug: str, session: DbSession) -> AthleteProfileOut:
    """Everything the athlete screen needs in one round-trip.

    Sections are independently empty-able: an athlete with no medals, records
    or rankings still returns a valid profile rather than 404-ing a section.
    """
    athlete = await get_athlete_by_slug(session, slug)
    if athlete is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Athlete not found")

    records = await athlete_records(session, athlete.id)
    holders = await record_holders(session, records)
    medals = await athlete_medals(session, athlete.id)
    results = await athlete_recent_results(session, athlete.id)
    rankings = await athlete_rankings(session, athlete.id)

    return AthleteProfileOut(
        athlete=AthleteDetailOut.model_validate(athlete).model_dump(mode="json"),
        disciplines=[DisciplineOut.model_validate(d) for d in athlete.disciplines],
        personal_bests=[_record_out(r, holders) for r in records],
        medals=[
            {
                "metal": m.metal,
                "event_name": m.event_name,
                "edition_label": m.edition.label if m.edition else None,
                "competition_name": (
                    m.edition.competition.name if m.edition and m.edition.competition else None
                ),
                "discipline": m.discipline.name if m.discipline else None,
            }
            for m in medals
        ],
        recent_results=[
            {
                "event_id": event.id,
                "event_name": event.name,
                "phase": event.phase,
                "status": event.status,
                "scheduled_start": (
                    event.scheduled_start.isoformat() if event.scheduled_start else None
                ),
                "discipline": event.discipline.name if event.discipline else None,
                "competition_name": (
                    event.edition.competition.name
                    if event.edition and event.edition.competition
                    else None
                ),
                "position": result.position,
                "result_status": result.status,
                "value_text": result.value_text,
                "value_kind": result.value_kind,
            }
            for result, event in results
        ],
        world_rankings=[
            {
                "methodology": r.methodology,
                "rank": r.rank,
                "points": r.points,
                "as_of": r.as_of.isoformat(),
                "discipline": r.discipline.name if r.discipline else None,
            }
            for r in rankings
        ],
    )
