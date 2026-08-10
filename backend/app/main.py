from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import text

from app.ai.deterministic import DeterministicProvider
from app.ai.llm import LLMProvider
from app.api.v1 import router as v1_router
from app.core.cache import Cache
from app.core.config import get_settings
from app.core.logging import configure_logging
from app.core.middleware import RateLimitMiddleware, RequestContextMiddleware
from app.db.session import get_session_factory
from app.services.live import LiveHub


@asynccontextmanager
async def lifespan(app: FastAPI):
    await app.state.live_hub.start()
    yield
    await app.state.live_hub.stop()
    await app.state.cache.close()


def _build_ai_provider(settings):
    """Pick the configured AI provider.

    Falls back to the deterministic provider when an LLM is selected but not
    fully configured — starting with a half-configured model would fail every
    insight request at runtime instead of at boot.
    """
    if settings.ai_provider == "llm" and settings.ai_api_key:
        return LLMProvider(
            endpoint=settings.ai_endpoint,
            api_key=settings.ai_api_key,
            model=settings.ai_model,
        )
    return DeterministicProvider()


def create_app() -> FastAPI:
    settings = get_settings()
    configure_logging(settings.log_level)
    app = FastAPI(
        title=settings.app_name,
        version="0.2.0",
        description="Multi-sport platform API — see /docs for the OpenAPI explorer.",
        lifespan=lifespan,
    )
    # Order matters: the limiter runs before handlers so rejected requests
    # never touch the database, and the context middleware wraps everything
    # so even a 429 is logged with its request id.
    app.add_middleware(
        RateLimitMiddleware,
        limit=settings.auth_rate_limit,
        window_seconds=settings.auth_rate_window_seconds,
        paths=("/api/v1/auth/login", "/api/v1/auth/register"),
        trusted_proxies=frozenset(settings.trusted_proxies),
    )
    app.add_middleware(RequestContextMiddleware)
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
    app.state.ai_provider = _build_ai_provider(settings)
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
