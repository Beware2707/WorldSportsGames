from fastapi import APIRouter

from app.api.deps import DbSession, OptionalUser
from app.schemas.home import HomeFeed
from app.services.home import build_home_feed

router = APIRouter(tags=["home"])


@router.get("/home", response_model=HomeFeed)
async def home(session: DbSession, user: OptionalUser) -> HomeFeed:
    """Personalized when a valid token is supplied, generic otherwise.

    Deliberately public: an anonymous visitor still gets a full feed.
    """
    return await build_home_feed(session, user_id=user.id if user else None)
