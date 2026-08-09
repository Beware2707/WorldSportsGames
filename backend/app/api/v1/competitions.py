from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, status

from app.api.deps import DbSession
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.competition import competitions_query, get_competition_by_slug
from app.schemas.competition import CompetitionDetailOut, CompetitionOut

router = APIRouter(tags=["competitions"])


@router.get("/competitions", response_model=Page[CompetitionOut])
async def list_competitions(
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    level: Annotated[
        str | None, Query(pattern="^(olympic|world|continental|league|national|other)$")
    ] = None,
    sport: Annotated[str | None, Query()] = None,
    sort: Annotated[str | None, Query(pattern="^-?name$")] = None,
) -> Page[CompetitionOut]:
    items, total, pages = await paginate(
        session, competitions_query(level=level, sport=sport, sort=sort), params
    )
    return Page(
        items=[CompetitionOut.model_validate(c) for c in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/competitions/{slug}", response_model=CompetitionDetailOut)
async def get_competition(slug: str, session: DbSession) -> CompetitionDetailOut:
    competition = await get_competition_by_slug(session, slug)
    if competition is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Competition not found")
    return CompetitionDetailOut.model_validate(competition)
