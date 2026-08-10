"""Request correlation, latency measurement and rate limiting."""

import logging
import time
import uuid
from collections import defaultdict, deque

from fastapi import Request, Response, status
from fastapi.responses import JSONResponse
from starlette.middleware.base import BaseHTTPMiddleware

from app.core.logging import request_id_var

logger = logging.getLogger("app.request")


class RequestContextMiddleware(BaseHTTPMiddleware):
    """Assign a request id, time the request, and log the outcome.

    The id is echoed in ``X-Request-ID`` so a user can quote it in a bug
    report and it can be found in the logs.
    """

    async def dispatch(self, request: Request, call_next):
        request_id = request.headers.get("X-Request-ID") or uuid.uuid4().hex[:16]
        token = request_id_var.set(request_id)
        started = time.perf_counter()
        try:
            response = await call_next(request)
        except Exception:
            duration_ms = (time.perf_counter() - started) * 1000
            logger.exception(
                "request failed",
                extra={
                    "context": {
                        "method": request.method,
                        "path": request.url.path,
                        "duration_ms": round(duration_ms, 2),
                    }
                },
            )
            request_id_var.reset(token)
            raise

        duration_ms = (time.perf_counter() - started) * 1000
        response.headers["X-Request-ID"] = request_id
        # Latency is on every line, so slow endpoints are queryable without
        # a separate metrics pipeline.
        logger.info(
            "request",
            extra={
                "context": {
                    "method": request.method,
                    "path": request.url.path,
                    "status": response.status_code,
                    "duration_ms": round(duration_ms, 2),
                }
            },
        )
        request_id_var.reset(token)
        return response


class RateLimitMiddleware(BaseHTTPMiddleware):
    """Fixed-window limiter for credential endpoints.

    Deliberately narrow: it protects login and registration, where unlimited
    attempts are the actual risk, and leaves read traffic alone.

    In-process, so with N replicas the effective limit is N×. That is a real
    limitation, documented rather than hidden — a shared Redis counter is the
    upgrade path when the platform runs more than one API instance.
    """

    def __init__(self, app, *, limit: int, window_seconds: int, paths: tuple[str, ...]):
        super().__init__(app)
        self._limit = limit
        self._window = window_seconds
        self._paths = paths
        self._hits: dict[str, deque[float]] = defaultdict(deque)

    def _client_key(self, request: Request) -> str:
        # X-Forwarded-For is only meaningful behind a trusted proxy; the
        # direct peer is the fallback.
        forwarded = request.headers.get("X-Forwarded-For")
        if forwarded:
            return forwarded.split(",")[0].strip()
        return request.client.host if request.client else "unknown"

    async def dispatch(self, request: Request, call_next) -> Response:
        if not request.url.path.startswith(self._paths):
            return await call_next(request)

        now = time.monotonic()
        key = f"{self._client_key(request)}:{request.url.path}"
        hits = self._hits[key]
        while hits and now - hits[0] > self._window:
            hits.popleft()

        if len(hits) >= self._limit:
            retry_after = int(self._window - (now - hits[0])) + 1
            logger.warning(
                "rate limit exceeded",
                extra={"context": {"path": request.url.path}},
            )
            return JSONResponse(
                status_code=status.HTTP_429_TOO_MANY_REQUESTS,
                content={"detail": "Too many attempts. Try again shortly."},
                headers={"Retry-After": str(retry_after)},
            )

        hits.append(now)
        return await call_next(request)
