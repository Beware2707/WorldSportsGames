from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import text

from app.api.v1 import router as v1_router
from app.core.cache import Cache
from app.core.config import get_settings
from app.db.session import get_session_factory
from app.services.live import LiveHub


@asynccontextmanager
async def lifespan(app: FastAPI):
    await app.state.live_hub.start()
    yield
    await app.state.live_hub.stop()
    await app.state.cache.close()


def create_app() -> FastAPI:
    settings = get_settings()
    app = FastAPI(
        title=settings.app_name,
        version="0.2.0",
        description="Multi-sport platform API — see /docs for the OpenAPI explorer.",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=settings.cors_origins,
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.include_router(v1_router)

    # Constructed eagerly so routes work without lifespan (e.g. test transports);
    # lifespan only manages network resources (Redis reader, connections).
    app.state.cache = Cache(settings.redis_url)
    app.state.live_hub = LiveHub(settings.redis_url)
    app.state.session_factory = get_session_factory()
    app.state.background_tasks = set()

    @app.get("/health", tags=["ops"])
    async def health() -> dict:
        db_ok = True
        try:
            async with get_session_factory()() as session:
                await session.execute(text("SELECT 1"))
        except Exception:
            db_ok = False
        return {"status": "ok" if db_ok else "degraded", "database": db_ok}

    return app


app = create_app()
