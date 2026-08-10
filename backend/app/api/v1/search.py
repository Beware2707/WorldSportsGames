from typing import Annotated

from fastapi import APIRouter, Query

from app.api.deps import DbSession
from app.schemas.search import SearchResults, Suggestions
from app.services.search import global_search, suggest

router = APIRouter(tags=["search"])


@router.get("/search", response_model=SearchResults)
async def search(
    session: DbSession, q: Annotated[str, Query(min_length=2, max_length=80)]
) -> SearchResults:
    return await global_search(session, q)


@router.get("/search/suggest", response_model=Suggestions)
async def search_suggest(
    session: DbSession, q: Annotated[str, Query(min_length=1, max_length=80)]
) -> Suggestions:
    """Ranked autocomplete for the search bar (prefix matches first)."""
    return await suggest(session, q)
