"""Fail-open guarantees for Redis-backed infrastructure.

The rest of the suite runs with Redis disabled, so these paths would otherwise
never execute. Here we construct Cache/LiveHub with a URL and inject clients
that raise, asserting requests still succeed and updates still reach clients.
"""

import pytest

from app.core.cache import Cache
from app.services.live import LiveHub


class ExplodingRedis:
    """Every operation fails, as if Redis were down mid-request."""

    def __init__(self) -> None:
        self.published: list = []

    async def get(self, key):
        raise ConnectionError("redis down")

    async def set(self, key, value, ex=None):
        raise ConnectionError("redis down")

    async def publish(self, channel, message):
        raise ConnectionError("redis down")

    async def aclose(self):
        pass


class UnserializableValue:
    pass


async def test_cache_serves_from_source_when_redis_read_fails():
    cache = Cache("redis://unused")
    cache._client = ExplodingRedis()
    calls = 0

    async def produce():
        nonlocal calls
        calls += 1
        return {"items": [1, 2, 3]}

    assert await cache.get_or_set("k", 60, produce) == {"items": [1, 2, 3]}
    assert calls == 1


async def test_cache_survives_unserializable_value():
    """A write failure must not fail the request that produced the value."""
    cache = Cache("redis://unused")

    class ReadMissWriteOk:
        async def get(self, key):
            return None

        async def set(self, key, value, ex=None):
            raise ConnectionError("redis down")

        async def aclose(self):
            pass

    cache._client = ReadMissWriteOk()
    assert await cache.get_or_set("k", 60, lambda: _value({"ok": True})) == {"ok": True}


async def _value(v):
    return v


async def test_cache_disabled_without_url():
    cache = Cache("")
    assert await cache.get_or_set("k", 60, lambda: _value(7)) == 7


class FakeSocket:
    def __init__(self, fail: bool = False) -> None:
        self.fail = fail
        self.sent: list = []

    async def send_json(self, message):
        if self.fail:
            raise RuntimeError("client gone")
        self.sent.append(message)


async def test_hub_publishes_locally_when_redis_publish_fails():
    hub = LiveHub("redis://unused")
    hub._redis = ExplodingRedis()
    good = FakeSocket()
    await hub.register(good)

    await hub.publish({"type": "update", "seq": 1})

    assert good.sent == [{"type": "update", "seq": 1}]


async def test_hub_prunes_dead_clients_and_still_delivers():
    hub = LiveHub("")
    dead, alive = FakeSocket(fail=True), FakeSocket()
    await hub.register(dead)
    await hub.register(alive)

    await hub.publish({"type": "update", "seq": 2})

    assert alive.sent == [{"type": "update", "seq": 2}]
    assert dead not in hub._clients
    assert alive in hub._clients


async def test_hub_start_falls_back_to_local_when_redis_unreachable():
    # Port 1 is reliably closed; start() must swallow it and stay usable.
    hub = LiveHub("redis://127.0.0.1:1/0")
    await hub.start()
    assert hub.distributed is False

    client = FakeSocket()
    await hub.register(client)
    await hub.publish({"type": "update", "seq": 3})
    assert client.sent == [{"type": "update", "seq": 3}]
    await hub.stop()


async def test_hub_stop_survives_crashed_reader():
    """A reader that died on a Redis outage must not abort lifespan teardown."""
    import asyncio

    hub = LiveHub("redis://unused")

    async def crashing():
        raise ConnectionError("redis vanished")

    hub._reader_task = asyncio.create_task(crashing())
    await asyncio.sleep(0)  # let it fail
    await hub.stop()  # must not raise


@pytest.mark.parametrize("page,expected_first", [(1, 0), (2, 20)])
async def test_pagination_slices_are_disjoint_and_ordered(client, page, expected_first):
    """Offset arithmetic and ordering must hold beyond page 1."""
    r = await client.get("/api/v1/sports", params={"page": page, "size": 20, "sort": "name"})
    body = r.json()
    assert body["page"] == page
    names = [s["name"] for s in body["items"]]
    assert names == sorted(names)

    r_all = await client.get("/api/v1/sports", params={"size": 100, "sort": "name"})
    all_names = [s["name"] for s in r_all.json()["items"]]
    assert names == all_names[expected_first : expected_first + 20]


async def test_page_past_end_is_empty_not_error(client):
    r = await client.get("/api/v1/sports", params={"page": 99, "size": 20})
    assert r.status_code == 200
    assert r.json()["items"] == []
