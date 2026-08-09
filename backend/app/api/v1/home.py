from fastapi import APIRouter

from app.api.deps import DbSession
from app.schemas.home import HomeFeed
from app.services.home import build_home_feed

router = APIRouter(tags=["home"])


@router.get("/home", response_model=HomeFeed)
async def home(session: DbSession) -> HomeFeed:
    return await build_home_feed(session)
