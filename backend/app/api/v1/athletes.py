from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, status

from app.api.deps import DbSession
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.athlete import athletes_query, get_athlete_by_slug
from app.schemas.athlete import AthleteDetailOut, AthleteOut

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
