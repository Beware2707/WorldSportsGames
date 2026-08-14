import math
from contextlib import asynccontextmanager
from typing import Any

from fastapi import FastAPI, Request, status
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
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


def _sanitize(value: Any) -> Any:
    """Coerce a validation-error structure into something JSON can encode.

    Two hazards: non-finite floats (the NaN/inf we reject echoed back as
    ``input``) and arbitrary objects Pydantic attaches under ``ctx`` (e.g. the
    ``ValueError`` a field validator raised). Both become strings so the 422
    body always serializes."""
    if isinstance(value, bool | int | str) or value is None:
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else str(value)
    if isinstance(value, dict):
        return {str(k): _sanitize(v) for k, v in value.items()}
    if isinstance(value, list | tuple):
        return [_sanitize(v) for v in value]
    return str(value)  # exceptions and any other non-JSON object


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

    @app.exception_handler(RequestValidationError)
    async def _validation_handler(request: Request, exc: RequestValidationError):
        """Return a clean 422, stripping non-finite values from the echo.

        FastAPI's default handler echoes the offending input back in the error
        body; when that input is NaN/Infinity (which we reject on purpose),
        the response itself fails to serialize (JSON has no NaN), turning our
        422 into a 500. Sanitizing the echo keeps the rejection a 422.
        """
        return JSONResponse(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            content={"detail": _sanitize(exc.errors())},
        )

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
