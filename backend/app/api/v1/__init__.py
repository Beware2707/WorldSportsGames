from fastapi import APIRouter

from app.api.v1 import (
    ai,
    athletes,
    auth,
    competitions,
    competitive,
    countries,
    events,
    games,
    home,
    live,
    media,
    search,
    sports,
    users,
)

router = APIRouter(prefix="/api/v1")
router.include_router(auth.router)
router.include_router(home.router)
router.include_router(sports.router)
router.include_router(countries.router)
router.include_router(athletes.router)
router.include_router(competitions.router)
router.include_router(events.router)
router.include_router(competitive.router)
router.include_router(games.router)
router.include_router(ai.router)
router.include_router(media.router)
router.include_router(live.router)
router.include_router(search.router)
router.include_router(users.router)
