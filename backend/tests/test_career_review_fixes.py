"""Regressions for the career-track adversarial review findings.

Each test here failed against the code as originally written.
"""

import pytest

from app.services.career import merge_saves

CREDS = {"email": "fix@example.com", "password": "fix-pass-111", "display_name": "Fix"}


async def _login(client, creds=CREDS) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


@pytest.fixture
async def headers(client) -> dict:
    h = await _login(client)
    await client.post(
        "/api/v1/career/athlete", json={"name": "Fixer", "gender": "M"}, headers=h
    )
    return h


# ---- NaN / Infinity ------------------------------------------------------


@pytest.mark.parametrize("bad", ["NaN", "Infinity", "-Infinity"])
async def test_non_finite_value_num_is_rejected(client, headers, bad):
    """NaN/inf must be refused at the schema, not crash the handler.

    Sent as a raw JSON body because these tokens are not valid Python floats
    in a dict literal but stdlib json (and thus Starlette) accepts them."""
    r = await client.post(
        "/api/v1/career/results",
        content=f'{{"event": "sprint-100m", "value_num": {bad}, "reaction_ms": 150}}',
        headers={**headers, "content-type": "application/json"},
    )
    assert r.status_code == 422, f"{bad} value_num must be a 422, not {r.status_code}"


async def test_nan_reaction_cannot_launder_a_false_start(client, headers):
    """A real false start sent with reaction_ms=NaN must NOT validate.

    NaN < 100 is False, so before the fix the false-start floor passed and the
    run earned XP and a leaderboard entry."""
    r = await client.post(
        "/api/v1/career/results",
        content='{"event": "sprint-100m", "value_num": 11.95, "reaction_ms": NaN}',
        headers={**headers, "content-type": "application/json"},
    )
    assert r.status_code == 422

    # A NaN split must not defeat coherence either.
    r = await client.post(
        "/api/v1/career/results",
        content='{"event": "sprint-100m", "value_num": 11.95, "reaction_ms": 150, '
        '"splits": [NaN, 1, 1, 1, 1, 1, 1, 1, 1, 1]}',
        headers={**headers, "content-type": "application/json"},
    )
    assert r.status_code == 422


# ---- merge_saves robustness ----------------------------------------------


def test_merge_saves_handles_object_valued_lists():
    """Achievements/cosmetics are naturally lists of objects; the merge must
    union them without raising (set() on dicts is a TypeError)."""
    ours = {"achievements": [{"id": "a"}, {"id": "b"}], "total_xp": 500}
    theirs = {"achievements": [{"id": "b"}, {"id": "c"}], "total_xp": 300}
    merged = merge_saves(ours, theirs)
    assert merged["total_xp"] == 500
    ids = [a["id"] for a in merged["achievements"]]
    assert ids == ["a", "b", "c"], "object unlocks de-duplicated by value"


def test_merge_saves_tolerates_non_numeric_monotonic_values():
    merged = merge_saves({"total_xp": "corrupt"}, {"total_xp": 50})
    assert merged["total_xp"] == 50  # newer write, no crash

    merged = merge_saves({"unlocks": "not-a-list"}, {"unlocks": ["a"]})
    assert merged["unlocks"] == ["a"]


async def test_save_conflict_with_object_achievements_is_409_not_500(client, headers):
    """The exact reproduction: object-valued achievements through the conflict
    path must return the 409 merge suggestion, not a 500."""
    await client.put(
        "/api/v1/career/save",
        json={"payload": {"achievements": [{"id": "a"}]}, "base_version": 0},
        headers=headers,
    )
    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"achievements": [{"id": "b"}]}, "base_version": 0},
        headers=headers,
    )
    assert r.status_code == 409, r.text
    merge = r.json()["detail"]["suggested_merge"]
    assert [a["id"] for a in merge["achievements"]] == ["a", "b"]


# ---- cloud save concurrency (DB-level guard) -----------------------------


async def test_save_uses_conditional_update_not_check_then_set(
    client, headers, db_sessionmaker
):
    """Two writes from the same base_version: exactly one wins; the other 409s.

    Simulated by racing the conditional UPDATE directly — SQLite is
    single-writer so a true async race isn't reproducible, but the guard is a
    row-count-checked UPDATE ... WHERE version, which either matches or does
    not."""
    from sqlalchemy import update

    from app.models import CareerSave

    user_id = (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]
    await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 10}, "base_version": 0},
        headers=headers,
    )  # version 1

    # First device's conditional write from v1 succeeds.
    async with db_sessionmaker() as session:
        r1 = await session.execute(
            update(CareerSave)
            .where(CareerSave.user_id == user_id, CareerSave.version == 1)
            .values(payload={"total_xp": 20}, version=2)
        )
        await session.commit()
        assert r1.rowcount == 1

    # Second device, still on v1, now matches nothing → conflict at the API.
    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 15}, "base_version": 1},
        headers=headers,
    )
    assert r.status_code == 409
    assert r.json()["detail"]["server"]["version"] == 2


# ---- unbounded JSON ------------------------------------------------------


async def test_oversized_appearance_rejected(client):
    h = await _login(client, {"email": "big@example.com",
                              "password": "big-pass-111", "display_name": "Big"})
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "Big", "gender": "M", "appearance": {"blob": "x" * 9000}},
        headers=h,
    )
    assert r.status_code == 422


async def test_oversized_save_payload_rejected(client, headers):
    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"blob": "x" * 9000}, "base_version": 0},
        headers=headers,
    )
    assert r.status_code == 422


# ---- rate limit is genuinely per-athlete ---------------------------------


def _run(time_s: float) -> dict:
    split = (time_s - 0.16) / 10
    return {
        "event": "sprint-100m",
        "value_num": time_s,
        "reaction_ms": 160,
        "splits": [round(split, 4)] * 10,
    }


async def test_rate_limit_is_per_athlete_with_the_right_reason(client, headers):
    """Prove the cap is per-athlete (not global) and cites the rate reason."""
    reasons = []
    for i in range(12):
        r = await client.post(
            "/api/v1/career/results", json=_run(12.5 + i * 0.01), headers=headers
        )
        body = r.json()
        if not body["accepted"]:
            reasons.append(body["rejection_reason"])
    assert reasons, "the cap must fire"
    assert all("too many results" in reason for reason in reasons), reasons

    # A DIFFERENT athlete is unaffected — proving the cap is not global.
    other = await _login(
        client, {"email": "fresh@example.com", "password": "fresh-pass-1",
                 "display_name": "Fresh"}
    )
    await client.post(
        "/api/v1/career/athlete", json={"name": "Fresh Legs", "gender": "F"},
        headers=other,
    )
    r = await client.post("/api/v1/career/results", json=_run(12.9), headers=other)
    assert r.json()["accepted"] is True, "a different athlete must not be rate-limited"


async def test_wind_assisted_time_is_rejected(client, headers):
    """A +3.0 m/s tailwind is wind-assisted and cannot count."""
    run = _run(11.95)
    run["wind"] = 3.0
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    body = r.json()
    assert body["accepted"] is False
    assert "wind" in body["rejection_reason"]

    # Legal wind (+1.8) at the same time is fine.
    run["wind"] = 1.8
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert r.json()["accepted"] is True


async def test_slow_reaction_and_short_split_are_rejected(client, headers):
    """The two anti-cheat branches the review found had no negative test."""
    # Implausibly slow reaction (> 2000 ms).
    run = _run(12.5)
    run["reaction_ms"] = 2500
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert r.json()["accepted"] is False
    assert "reaction" in r.json()["rejection_reason"]

    # A zero split (non-positive).
    run = _run(12.5)
    run["splits"] = [0.0] + run["splits"][1:]
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert r.json()["accepted"] is False
