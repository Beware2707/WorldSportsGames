from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, status

from app.api.deps import DbSession
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.catalog import disciplines_query, get_sport_by_code, sports_query
from app.schemas.catalog import DisciplineOut, SportDetailOut, SportOut

router = APIRouter(tags=["sports"])

Params = Annotated[PageParams, Depends(page_params)]


@router.get("/sports", response_model=Page[SportOut])
async def list_sports(
    session: DbSession,
    params: Params,
    category: Annotated[str | None, Query(pattern="^(summer|winter|la28)$")] = None,
    sort: Annotated[str | None, Query(pattern="^-?(name|sort_order)$")] = None,
) -> Page[SportOut]:
    items, total, pages = await paginate(session, sports_query(category, sort), params)
    return Page(
        items=[SportOut.model_validate(s) for s in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/sports/{code}", response_model=SportDetailOut)
async def get_sport(code: str, session: DbSession) -> SportDetailOut:
    sport = await get_sport_by_code(session, code)
    if sport is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Sport not found")
    return SportDetailOut.model_validate(sport)


@router.get("/disciplines", response_model=Page[DisciplineOut])
async def list_disciplines(
    session: DbSession,
    params: Params,
    sport: Annotated[str | None, Query()] = None,
) -> Page[DisciplineOut]:
    items, total, pages = await paginate(session, disciplines_query(sport), params)
    return Page(
        items=[DisciplineOut.model_validate(d) for d in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )
