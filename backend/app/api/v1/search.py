from typing import Annotated

from fastapi import APIRouter, Query

from app.api.deps import DbSession
from app.schemas.search import SearchResults
from app.services.search import global_search

router = APIRouter(tags=["search"])


@router.get("/search", response_model=SearchResults)
async def search(
    session: DbSession, q: Annotated[str, Query(min_length=2, max_length=80)]
) -> SearchResults:
    return await global_search(session, q)
