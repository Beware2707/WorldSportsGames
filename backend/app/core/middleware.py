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

    def __init__(
        self,
        app,
        *,
        limit: int,
        window_seconds: int,
        paths: tuple[str, ...],
        trusted_proxies: frozenset[str] = frozenset(),
        max_tracked_clients: int = 10_000,
    ):
        super().__init__(app)
        self._limit = limit
        self._window = window_seconds
        self._paths = paths
        self._trusted_proxies = trusted_proxies
        self._max_tracked = max_tracked_clients
        self._hits: dict[str, deque[float]] = defaultdict(deque)

    def _client_key(self, request: Request) -> str:
        """Identify the caller by an address they cannot choose.

        ``X-Forwarded-For`` is attacker-controlled unless the immediate peer
        is a proxy we trust: real proxies *append*, so the leftmost entry is
        always client-written. Trusting it let anyone defeat the limit
        entirely by rotating the header, which is worse than no limit at all
        because it looks protected. We therefore key on the socket peer, and
        consult XFF only when that peer is an explicitly trusted proxy —
        taking the rightmost hop, which is the one that proxy observed.
        """
        peer = request.client.host if request.client else "unknown"
        if peer in self._trusted_proxies:
            forwarded = request.headers.get("X-Forwarded-For")
            if forwarded:
                hops = [h.strip() for h in forwarded.split(",") if h.strip()]
                if hops:
                    return hops[-1]
        return peer

    def _evict_stale(self, now: float) -> None:
        """Drop windows that have fully expired.

        Without this the key map grows for every distinct caller forever —
        an unbounded memory leak on a long-lived process.
        """
        expired = [
            key
            for key, hits in self._hits.items()
            if not hits or now - hits[-1] > self._window
        ]
        for key in expired:
            del self._hits[key]

    async def dispatch(self, request: Request, call_next) -> Response:
        if not request.url.path.startswith(self._paths):
            return await call_next(request)

        now = time.monotonic()
        if len(self._hits) >= self._max_tracked:
            self._evict_stale(now)

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
