from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request

from app.api.deps import DbSession
from app.core.config import get_settings
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.catalog import countries_query
from app.schemas.catalog import CountryOut

router = APIRouter(tags=["countries"])


@router.get("/countries", response_model=Page[CountryOut])
async def list_countries(
    request: Request,
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    sort: Annotated[str | None, Query(pattern="^-?name$")] = None,
) -> dict:
    async def produce() -> dict:
        items, total, pages = await paginate(session, countries_query(sort), params)
        return Page(
            items=[CountryOut.model_validate(c) for c in items],
            total=total,
            page=params.page,
            size=params.size,
            pages=pages,
        ).model_dump(mode="json")

    key = f"v1:countries:{sort}:{params.page}:{params.size}"
    return await request.app.state.cache.get_or_set(
        key, get_settings().cache_ttl_seconds, produce
    )
