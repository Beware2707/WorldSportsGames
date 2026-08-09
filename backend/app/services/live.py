"""Live update fan-out.

``LiveHub`` broadcasts live-update messages to connected WebSocket clients.
With Redis configured, messages go through a pub/sub channel so every API
replica fans out to its own clients; without Redis (tests, minimal dev) it
degrades to in-process broadcast. All failures fail open — live coverage
must never take the API down.
"""

import asyncio
import contextlib
import json
import logging
from typing import Any

from fastapi import WebSocket

logger = logging.getLogger(__name__)

LIVE_CHANNEL = "sports:live"


class LiveHub:
    def __init__(self, redis_url: str) -> None:
        self._redis_url = redis_url
        self._redis: Any = None
        self._pubsub: Any = None
        self._reader_task: asyncio.Task | None = None
        self._clients: set[WebSocket] = set()
        self._lock = asyncio.Lock()

    @property
    def distributed(self) -> bool:
        return self._redis is not None

    async def start(self) -> None:
        """Connect to Redis and start the fan-in reader. Fail-open to local mode."""
        if not self._redis_url:
            return
        try:
            import redis.asyncio as redis

            self._redis = redis.from_url(
                self._redis_url,
                decode_responses=True,
                socket_connect_timeout=2,
                socket_timeout=5,
            )
            await self._redis.ping()
            self._pubsub = self._redis.pubsub()
            await self._pubsub.subscribe(LIVE_CHANNEL)
            self._reader_task = asyncio.create_task(self._reader())
            logger.info("LiveHub: distributed mode via Redis")
        except Exception:
            logger.warning("LiveHub: Redis unavailable, falling back to local broadcast")
            self._redis = None
            self._pubsub = None

    async def stop(self) -> None:
        if self._reader_task is not None:
            self._reader_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._reader_task
            self._reader_task = None
        if self._pubsub is not None:
            with contextlib.suppress(Exception):
                await self._pubsub.aclose()
            self._pubsub = None
        if self._redis is not None:
            with contextlib.suppress(Exception):
                await self._redis.aclose()
            self._redis = None

    async def register(self, ws: WebSocket) -> None:
        async with self._lock:
            self._clients.add(ws)

    async def unregister(self, ws: WebSocket) -> None:
        async with self._lock:
            self._clients.discard(ws)

    async def publish(self, message: dict) -> None:
        """Publish a message to every client on every replica."""
        if self._redis is not None:
            try:
                await self._redis.publish(LIVE_CHANNEL, json.dumps(message))
                return
            except Exception:
                logger.warning("LiveHub: Redis publish failed, broadcasting locally")
        await self._broadcast_local(message)

    async def _broadcast_local(self, message: dict) -> None:
        async with self._lock:
            clients = list(self._clients)
        for ws in clients:
            try:
                await ws.send_json(message)
            except Exception:
                await self.unregister(ws)

    async def _reader(self) -> None:
        assert self._pubsub is not None
        async for raw in self._pubsub.listen():
            if raw.get("type") != "message":
                continue
            try:
                await self._broadcast_local(json.loads(raw["data"]))
            except Exception:
                logger.exception("LiveHub: dropping malformed pub/sub message")
