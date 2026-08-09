from math import ceil
from typing import Annotated

from fastapi import Query
from pydantic import BaseModel
from sqlalchemy import Select, func, select
from sqlalchemy.ext.asyncio import AsyncSession

MAX_PAGE_SIZE = 100


class PageParams(BaseModel):
    page: int = 1
    size: int = 20


def page_params(
    page: Annotated[int, Query(ge=1)] = 1,
    size: Annotated[int, Query(ge=1, le=MAX_PAGE_SIZE)] = 20,
) -> PageParams:
    return PageParams(page=page, size=size)


class Page[T](BaseModel):
    items: list[T]
    total: int
    page: int
    size: int
    pages: int


async def paginate(
    session: AsyncSession, query: Select, params: PageParams
) -> tuple[list, int, int]:
    """Run a paginated query. Returns (rows, total, pages)."""
    total = (
        await session.execute(select(func.count()).select_from(query.order_by(None).subquery()))
    ).scalar_one()
    rows = (
        (await session.execute(query.offset((params.page - 1) * params.size).limit(params.size)))
        .scalars()
        .unique()
        .all()
    )
    return list(rows), total, max(1, ceil(total / params.size))
