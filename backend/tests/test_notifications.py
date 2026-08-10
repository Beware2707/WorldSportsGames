"""Notification generation and inbox tests.

The invariants: an opted-out kind leaves no trace at all, generation is
idempotent, and one user can never read another's inbox.
"""

import pytest
from sqlalchemy import select

from app.models import Athlete, Notification
from app.services.notifications import notify, notify_medal

CREDS = {"email": "notify@example.com", "password": "notify-pass-1", "display_name": "Notify"}
OTHER = {"email": "other2@example.com", "password": "other-pass-22", "display_name": "Other"}


async def _login(client, creds) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


@pytest.fixture
async def headers(client) -> dict:
    return await _login(client, CREDS)


async def _user_id(client, headers) -> int:
    return (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]


async def test_inbox_requires_auth(client):
    assert (await client.get("/api/v1/notifications")).status_code == 401


async def test_inbox_starts_empty(client, headers):
    body = (await client.get("/api/v1/notifications", headers=headers)).json()
    assert body["items"] == []
    count = (await client.get("/api/v1/notifications/unread-count", headers=headers)).json()
    assert count["unread"] == 0


async def test_medal_notifies_athlete_and_country_followers(
    client, headers, db_sessionmaker
):
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    jamaica = next(c for c in countries["items"] if c["iso3"] == "JAM")
    athlete = (await client.get("/api/v1/athletes/zellie-dunbar")).json()

    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "athlete", "entity_id": athlete["id"]},
        headers=headers,
    )

    async with db_sessionmaker() as session:
        created = await notify_medal(
            session,
            athlete_id=athlete["id"],
            athlete_name="Zellie Dunbar",
            athlete_slug="zellie-dunbar",
            country_id=jamaica["id"],
            metal="gold",
            event_name="Women's 100m",
        )
        await session.commit()
    assert len(created) == 1

    body = (await client.get("/api/v1/notifications", headers=headers)).json()
    assert body["total"] == 1
    item = body["items"][0]
    assert item["kind"] == "medal_result"
    assert "gold" in item["body"].lower()
    assert item["payload"]["route"] == "/athletes/zellie-dunbar"
    assert item["read_at"] is None


async def test_generation_is_idempotent(client, headers, db_sessionmaker):
    athlete = (await client.get("/api/v1/athletes/kiptoo-cherop")).json()
    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "athlete", "entity_id": athlete["id"]},
        headers=headers,
    )

    async with db_sessionmaker() as session:
        for _ in range(3):
            await notify_medal(
                session,
                athlete_id=athlete["id"],
                athlete_name="Kiptoo Cherop",
                athlete_slug="kiptoo-cherop",
                country_id=1,
                metal="gold",
                event_name="Men's Marathon",
            )
        await session.commit()

    body = (await client.get("/api/v1/notifications", headers=headers)).json()
    assert body["total"] == 1, "re-running generation must not double-notify"


async def test_opting_out_leaves_no_row_at_all(client, headers, db_sessionmaker):
    """A disabled kind must not be stored-and-hidden — it must not exist."""
    await client.put(
        "/api/v1/users/me/notifications",
        json={"preferences": [{"kind": "medal_result", "enabled": False}]},
        headers=headers,
    )
    user_id = await _user_id(client, headers)

    async with db_sessionmaker() as session:
        created = await notify(
            session,
            user_ids=[user_id],
            kind="medal_result",
            title="Gold",
            body="Someone won gold",
            payload={},
            dedupe_key="medal:test:1",
        )
        await session.commit()
    assert created == []

    async with db_sessionmaker() as session:
        rows = (await session.execute(select(Notification))).scalars().all()
    assert rows == [], "an opted-out notification was persisted anyway"


async def test_other_kinds_still_arrive_after_one_opt_out(
    client, headers, db_sessionmaker
):
    await client.put(
        "/api/v1/users/me/notifications",
        json={"preferences": [{"kind": "medal_result", "enabled": False}]},
        headers=headers,
    )
    user_id = await _user_id(client, headers)

    async with db_sessionmaker() as session:
        created = await notify(
            session,
            user_ids=[user_id],
            kind="record_broken",
            title="World record",
            body="A record fell",
            payload={},
            dedupe_key="record:test:1",
        )
        await session.commit()
    assert len(created) == 1


async def test_marking_read_and_unread_count(client, headers, db_sessionmaker):
    user_id = await _user_id(client, headers)
    async with db_sessionmaker() as session:
        for i in range(3):
            await notify(
                session,
                user_ids=[user_id],
                kind="breaking_news",
                title=f"News {i}",
                body="Something happened",
                payload={},
                dedupe_key=f"news:{i}",
            )
        await session.commit()

    assert (
        await client.get("/api/v1/notifications/unread-count", headers=headers)
    ).json()["unread"] == 3

    first = (await client.get("/api/v1/notifications", headers=headers)).json()["items"][0]
    r = await client.post(
        f"/api/v1/notifications/{first['id']}/read", headers=headers
    )
    assert r.status_code == 200
    assert r.json()["read_at"] is not None

    assert (
        await client.get("/api/v1/notifications/unread-count", headers=headers)
    ).json()["unread"] == 2

    unread = (
        await client.get(
            "/api/v1/notifications", params={"unread_only": True}, headers=headers
        )
    ).json()
    assert unread["total"] == 2

    assert (
        await client.post("/api/v1/notifications/read-all", headers=headers)
    ).status_code == 204
    assert (
        await client.get("/api/v1/notifications/unread-count", headers=headers)
    ).json()["unread"] == 0


async def test_one_user_cannot_read_or_mutate_anothers_inbox(
    client, headers, db_sessionmaker
):
    user_id = await _user_id(client, headers)
    async with db_sessionmaker() as session:
        await notify(
            session,
            user_ids=[user_id],
            kind="breaking_news",
            title="Private",
            body="Only for the owner",
            payload={},
            dedupe_key="private:1",
        )
        await session.commit()

    mine = (await client.get("/api/v1/notifications", headers=headers)).json()
    notification_id = mine["items"][0]["id"]

    other_headers = await _login(client, OTHER)
    assert (
        await client.get("/api/v1/notifications", headers=other_headers)
    ).json()["items"] == []

    r = await client.post(
        f"/api/v1/notifications/{notification_id}/read", headers=other_headers
    )
    assert r.status_code == 404, "another user's notification must be invisible"

    still_unread = (
        await client.get("/api/v1/notifications/unread-count", headers=headers)
    ).json()
    assert still_unread["unread"] == 1


async def test_followers_of_a_country_are_notified_too(
    client, headers, db_sessionmaker
):
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    kenya = next(c for c in countries["items"] if c["iso3"] == "KEN")
    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "country", "entity_id": kenya["id"]},
        headers=headers,
    )

    async with db_sessionmaker() as session:
        athlete = (
            await session.execute(select(Athlete).where(Athlete.slug == "kiptoo-cherop"))
        ).scalar_one()
        created = await notify_medal(
            session,
            athlete_id=athlete.id,
            athlete_name=athlete.full_name,
            athlete_slug=athlete.slug,
            country_id=kenya["id"],
            metal="gold",
            event_name="Men's Marathon",
        )
        await session.commit()

    assert len(created) == 1, "country followers should receive medal news"
