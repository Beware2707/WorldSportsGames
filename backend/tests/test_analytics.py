"""Analytics ingestion: batched, whitelisted, anonymous-friendly."""

from sqlalchemy import select

from app.models import AnalyticsEvent

USER = {"email": "metrics@example.com", "password": "metrics-pass-1", "display_name": "M"}


async def _login(client) -> dict:
    await client.post("/api/v1/auth/register", json=USER)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": USER["email"], "password": USER["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


async def test_batch_ingest(client, db_sessionmaker):
    headers = await _login(client)
    r = await client.post(
        "/api/v1/analytics/events",
        json={
            "events": [
                {"name": "SessionStarted", "properties": {"tier": "medium"}},
                {"name": "GameStarted", "properties": {"game": "sprint-100m"}},
                {"name": "GameCompleted",
                 "properties": {"game": "sprint-100m", "time": 12.4},
                 "client_ts": "2026-08-14T10:00:00Z"},
            ]
        },
        headers=headers,
    )
    assert r.status_code == 202
    assert r.json() == {"accepted": 3, "rejected": []}

    async with db_sessionmaker() as session:
        rows = (await session.execute(select(AnalyticsEvent))).scalars().all()
    assert len(rows) == 3
    assert all(row.user_id is not None for row in rows)


async def test_anonymous_events_are_allowed(client, db_sessionmaker):
    """SessionStarted fires before login — it must not be lost."""
    r = await client.post(
        "/api/v1/analytics/events",
        json={"events": [{"name": "SessionStarted"}]},
    )
    assert r.status_code == 202
    async with db_sessionmaker() as session:
        row = (await session.execute(select(AnalyticsEvent))).scalar_one()
    assert row.user_id is None


async def test_unknown_names_rejected_per_event_not_per_batch(client):
    r = await client.post(
        "/api/v1/analytics/events",
        json={
            "events": [
                {"name": "GameStarted"},
                {"name": "TotallyMadeUp"},
                {"name": "Crash"},
            ]
        },
    )
    body = r.json()
    assert body["accepted"] == 2, "one bad event must not lose the batch"
    assert body["rejected"][0]["index"] == 1
    assert "unknown event" in body["rejected"][0]["reason"]


async def test_oversized_properties_rejected(client):
    r = await client.post(
        "/api/v1/analytics/events",
        json={"events": [{"name": "Performance", "properties": {"blob": "x" * 5000}}]},
    )
    assert r.json()["accepted"] == 0
    assert "too large" in r.json()["rejected"][0]["reason"]


async def test_batch_size_is_capped(client):
    r = await client.post(
        "/api/v1/analytics/events",
        json={"events": [{"name": "Crash"}] * 101},
    )
    assert r.status_code == 422, "an unbounded batch is its own DoS"

    r = await client.post("/api/v1/analytics/events", json={"events": []})
    assert r.status_code == 422
