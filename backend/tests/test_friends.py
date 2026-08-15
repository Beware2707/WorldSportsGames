"""Friendship graph and the now-real friends leaderboards."""

import pytest

ALICE = {"email": "alice@example.com", "password": "alice-pass-1", "display_name": "Alice"}
BOB = {"email": "bob@example.com", "password": "bob-pass-111", "display_name": "Bob"}
CARA = {"email": "cara@example.com", "password": "cara-pass-11", "display_name": "Cara"}


async def _login(client, creds) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


@pytest.fixture
async def alice(client) -> dict:
    return await _login(client, ALICE)


@pytest.fixture
async def bob(client) -> dict:
    return await _login(client, BOB)


async def _befriend(client, requester_headers, addressee_headers) -> None:
    code = (
        await client.get("/api/v1/users/me/friends/code", headers=addressee_headers)
    ).json()["code"]
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": code}, headers=requester_headers
    )
    assert r.status_code == 201
    incoming = (
        await client.get("/api/v1/users/me/friends", headers=addressee_headers)
    ).json()
    pending = next(f for f in incoming if f["status"] == "pending")
    r = await client.post(
        f"/api/v1/users/me/friends/{pending['id']}/accept", headers=addressee_headers
    )
    assert r.status_code == 200


async def test_friend_code_is_stable_and_private(client, alice):
    first = (await client.get("/api/v1/users/me/friends/code", headers=alice)).json()
    second = (await client.get("/api/v1/users/me/friends/code", headers=alice)).json()
    assert first["code"] == second["code"], "the code must not rotate per request"
    assert len(first["code"]) == 8

    assert (await client.get("/api/v1/users/me/friends/code")).status_code == 401


async def test_request_accept_flow(client, alice, bob):
    code = (await client.get("/api/v1/users/me/friends/code", headers=bob)).json()["code"]
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": code}, headers=alice
    )
    assert r.status_code == 201
    assert r.json()["status"] == "pending"
    assert r.json()["direction"] == "outgoing"
    assert r.json()["display_name"] == "Bob"

    bob_list = (await client.get("/api/v1/users/me/friends", headers=bob)).json()
    incoming = next(f for f in bob_list if f["direction"] == "incoming")
    assert incoming["display_name"] == "Alice"

    accepted = (
        await client.post(
            f"/api/v1/users/me/friends/{incoming['id']}/accept", headers=bob
        )
    ).json()
    assert accepted["status"] == "accepted"

    alice_list = (await client.get("/api/v1/users/me/friends", headers=alice)).json()
    assert [f["status"] for f in alice_list] == ["accepted"]


async def test_unknown_code_404s_without_leaking(client, alice):
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": "ZZZZZZZZ"}, headers=alice
    )
    assert r.status_code == 404
    # The response must not distinguish "code never existed" from anything
    # else — codes are the anti-enumeration boundary.
    assert r.json()["detail"] == "Unknown friend code"


async def test_cannot_friend_yourself(client, alice):
    code = (await client.get("/api/v1/users/me/friends/code", headers=alice)).json()["code"]
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": code}, headers=alice
    )
    assert r.status_code == 422


async def test_duplicate_and_reverse_requests(client, alice, bob):
    bob_code = (await client.get("/api/v1/users/me/friends/code", headers=bob)).json()["code"]
    await client.post("/api/v1/users/me/friends", json={"code": bob_code}, headers=alice)

    # Repeat request → 409.
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": bob_code}, headers=alice
    )
    assert r.status_code == 409

    # Bob requesting Alice back = acceptance, not a second pending row.
    alice_code = (
        await client.get("/api/v1/users/me/friends/code", headers=alice)
    ).json()["code"]
    r = await client.post(
        "/api/v1/users/me/friends", json={"code": alice_code}, headers=bob
    )
    assert r.status_code == 201
    assert r.json()["status"] == "accepted"


async def test_only_the_addressee_can_accept(client, alice, bob):
    code = (await client.get("/api/v1/users/me/friends/code", headers=bob)).json()["code"]
    sent = (
        await client.post(
            "/api/v1/users/me/friends", json={"code": code}, headers=alice
        )
    ).json()
    # Alice (the requester) trying to accept her own request: invisible.
    r = await client.post(
        f"/api/v1/users/me/friends/{sent['id']}/accept", headers=alice
    )
    assert r.status_code == 404


async def test_unfriend_removes_for_both(client, alice, bob):
    await _befriend(client, alice, bob)
    friendship = (await client.get("/api/v1/users/me/friends", headers=alice)).json()[0]
    r = await client.delete(
        f"/api/v1/users/me/friends/{friendship['id']}", headers=alice
    )
    assert r.status_code == 204
    assert (await client.get("/api/v1/users/me/friends", headers=bob)).json() == []


async def test_games_friends_leaderboard_is_real_now(client, alice, bob):
    """The scope that was honestly empty must now show accepted friends."""
    cara = await _login(client, CARA)
    for headers, score in [(alice, 300.0), (bob, 250.0), (cara, 200.0)]:
        await client.post(
            "/api/v1/games/sprint-reaction/sessions",
            json={"score": score},
            headers=headers,
        )
    await _befriend(client, alice, bob)

    board = (
        await client.get(
            "/api/v1/games/sprint-reaction/leaderboard",
            params={"scope": "friends"},
            headers=alice,
        )
    ).json()
    names = [row["display_name"] for row in board["rows"]]
    assert names == ["Bob", "Alice"], "friends + self, best first; Cara excluded"


async def test_career_friends_leaderboard(client, alice, bob):
    cara = await _login(client, CARA)
    # Cara is faster than both but is NOT a friend — a no-op filter would
    # leak her onto the board (and to the top), so she is the discriminating
    # case the earlier version lacked.
    for headers, name, time in [
        (alice, "Alice A", 12.6),
        (bob, "Bob B", 12.3),
        (cara, "Cara C", 11.95),
    ]:
        await client.post(
            "/api/v1/career/athlete",
            json={"name": name, "gender": "X"},
            headers=headers,
        )
        split = (time - 0.16) / 10
        await client.post(
            "/api/v1/career/results",
            json={
                "event": "sprint-100m",
                "value_num": time,
                "reaction_ms": 160,
                "splits": [round(split, 4)] * 10,
            },
            headers=headers,
        )
    await _befriend(client, alice, bob)

    board = (
        await client.get(
            "/api/v1/career/leaderboard",
            params={"event": "sprint-100m", "scope": "friends"},
            headers=alice,
        )
    ).json()
    names = [row["athlete_name"] for row in board["rows"]]
    assert names == ["Bob B", "Alice A"], f"non-friend Cara leaked in: {names}"

    r = await client.get(
        "/api/v1/career/leaderboard",
        params={"event": "sprint-100m", "scope": "friends"},
    )
    assert r.status_code == 401, "friends scope requires sign-in"


async def test_reverse_pair_is_blocked_by_the_database(client, alice, bob, db_sessionmaker):
    """Two reverse requests racing must be stopped by the DB, not the pre-check.

    The directional (requester, addressee) constraint accepted A->B and B->A
    as different rows, so a true race left two rows for one pair. The
    undirected pair_key constraint is what the database can actually enforce
    — simulated here by inserting the reverse row directly, as a race would.
    """
    from sqlalchemy.exc import IntegrityError

    from app.models import Friendship

    alice_id = (await client.get("/api/v1/auth/me", headers=alice)).json()["id"]
    bob_id = (await client.get("/api/v1/auth/me", headers=bob)).json()["id"]
    key = f"{min(alice_id, bob_id)}:{max(alice_id, bob_id)}"

    async with db_sessionmaker() as session:
        session.add(Friendship(
            requester_id=alice_id, addressee_id=bob_id, pair_key=key))
        await session.commit()

    async with db_sessionmaker() as session:
        session.add(Friendship(
            requester_id=bob_id, addressee_id=alice_id, pair_key=key))
        with pytest.raises(IntegrityError):
            await session.commit()
