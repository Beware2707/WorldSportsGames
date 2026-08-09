"""Fail-open Redis JSON cache.

Read-through cache for catalogue endpoints. Any Redis problem (not
configured, unreachable, serialization error) silently falls back to the
producer — caching must never break a request. An empty ``redis_url``
disables caching entirely (used by the test suite).
"""

import contextlib
import json
import logging
from collections.abc import Awaitable, Callable
from typing import Any

logger = logging.getLogger(__name__)


class Cache:
    def __init__(self, redis_url: str) -> None:
        self._url = redis_url
        self._client: Any = None
        self._enabled = bool(redis_url)

    async def _get_client(self) -> Any:
        if self._client is None:
            import redis.asyncio as redis

            self._client = redis.from_url(
                self._url,
                decode_responses=True,
                socket_connect_timeout=1,
                socket_timeout=1,
            )
        return self._client

    async def get_or_set(
        self, key: str, ttl_seconds: int, producer: Callable[[], Awaitable[Any]]
    ) -> Any:
        """Return the cached JSON value for ``key`` or produce, cache and return it."""
        if not self._enabled:
            return await producer()
        try:
            client = await self._get_client()
            cached = await client.get(key)
            if cached is not None:
                return json.loads(cached)
        except Exception:
            logger.warning("cache read failed for %s; serving from source", key)
            return await producer()

        value = await producer()
        try:
            await client.set(key, json.dumps(value), ex=ttl_seconds)
        except Exception:
            logger.warning("cache write failed for %s", key)
        return value

    async def close(self) -> None:
        if self._client is not None:
            with contextlib.suppress(Exception):
                await self._client.aclose()
            self._client = None
