"""Career track: athlete, result validation, leaderboards, cloud save.

The validation tests are the anti-cheat contract for the Unreal client:
every rejection path must fire, every rejection must be stored for audit,
and nothing invalid may ever reach XP or a leaderboard.
"""

import pytest
from sqlalchemy import select

from app.models import GameResult
from app.services.career import (
    EVENTS,
    attribute_ceiling,
    merge_saves,
    stage_for_xp,
)

CREDS = {"email": "runner@example.com", "password": "career-pass-1", "display_name": "Runner"}
RIVAL = {"email": "rival2@example.com", "password": "career-pass-2", "display_name": "Rival"}


async def _login(client, creds) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


async def _kenya_id(client) -> int:
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    return next(c["id"] for c in countries["items"] if c["iso3"] == "KEN")


@pytest.fixture
async def headers(client) -> dict:
    return await _login(client, CREDS)


@pytest.fixture
async def athlete(client, headers) -> dict:
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "Kip Rono", "gender": "M", "country_id": await _kenya_id(client)},
        headers=headers,
    )
    assert r.status_code == 201, r.text
    return r.json()


def _valid_run(time_s: float = 12.5, reaction_ms: float = 180.0) -> dict:
    # Ten equal splits that sum (with reaction) to the recorded time.
    split = (time_s - reaction_ms / 1000.0) / 10
    return {
        "event": "sprint-100m",
        "value_num": time_s,
        "reaction_ms": reaction_ms,
        "splits": [round(split, 4)] * 10,
        "wind": 0.3,
        "rng_seed": "seed-1",
        "input_digest": "digest-1",
    }


# ---- athlete -------------------------------------------------------------


async def test_career_requires_auth(client):
    assert (await client.get("/api/v1/career/athlete")).status_code == 401
    assert (
        await client.post("/api/v1/career/athlete", json={"name": "X", "gender": "M"})
    ).status_code == 401


async def test_create_athlete_with_defaults(client, headers, athlete):
    assert athlete["name"] == "Kip Rono"
    assert athlete["career_stage"] == "rookie"
    assert athlete["total_xp"] == 0
    assert athlete["country"]["iso3"] == "KEN"
    # All seven attributes exist at the starting value.
    assert set(athlete["attributes"]) == {
        "reaction", "acceleration", "max_speed", "stride_efficiency",
        "stamina", "recovery", "technique",
    }
    assert all(v == 40.0 for v in athlete["attributes"].values())

    fetched = (await client.get("/api/v1/career/athlete", headers=headers)).json()
    assert fetched["id"] == athlete["id"]


async def test_one_athlete_per_user(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "Second", "gender": "F"},
        headers=headers,
    )
    assert r.status_code == 409


async def test_unknown_country_rejected(client, headers):
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "X", "gender": "M", "country_id": 999999},
        headers=headers,
    )
    assert r.status_code == 404


# ---- event catalogue -----------------------------------------------------


async def test_event_catalogue_lists_the_100m(client):
    r = await client.get("/api/v1/career/events")
    events = r.json()
    sprint = next(e for e in events if e["code"] == "sprint-100m")
    assert sprint["lower_is_better"] is True
    assert sprint["requires_reaction"] is True
    assert sprint["splits_expected"] == 10


# ---- result validation ---------------------------------------------------


async def test_valid_result_earns_xp_and_pb(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/results", json=_valid_run(12.5), headers=headers
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["accepted"] is True
    assert body["rejection_reason"] is None
    assert body["is_personal_best"] is True
    assert body["xp_awarded"] > 0
    assert body["value_text"] == "12.50"

    # A slower run is not a PB but still earns participation XP.
    slower = await client.post(
        "/api/v1/career/results", json=_valid_run(13.1), headers=headers
    )
    assert slower.json()["is_personal_best"] is False
    assert slower.json()["xp_awarded"] > 0
    assert slower.json()["total_xp"] > body["total_xp"]


async def test_impossible_time_rejected_but_stored(
    client, headers, athlete, db_sessionmaker
):
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "sprint-100m", "value_num": 5.0, "reaction_ms": 150},
        headers=headers,
    )
    assert r.status_code == 201
    body = r.json()
    assert body["accepted"] is False
    assert "plausible range" in body["rejection_reason"]
    assert body["xp_awarded"] == 0

    # Stored for audit, marked invalid — not discarded.
    async with db_sessionmaker() as session:
        rows = (await session.execute(select(GameResult))).scalars().all()
    assert len(rows) == 1
    assert rows[0].is_valid is False
    assert rows[0].rejection_reason


async def test_false_start_rejected(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "sprint-100m", "value_num": 12.0, "reaction_ms": 80},
        headers=headers,
    )
    body = r.json()
    assert body["accepted"] is False
    assert "false start" in body["rejection_reason"]


async def test_missing_reaction_rejected(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "sprint-100m", "value_num": 12.0},
        headers=headers,
    )
    assert r.json()["accepted"] is False
    assert "reaction" in r.json()["rejection_reason"]


async def test_incoherent_splits_rejected(client, headers, athlete):
    run = _valid_run(12.5)
    run["splits"] = [1.0] * 10  # sums to 10.0, not 12.5
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert r.json()["accepted"] is False
    assert "do not sum" in r.json()["rejection_reason"]

    run = _valid_run(12.5)
    run["splits"] = run["splits"][:5]  # wrong count
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert "expected 10 splits" in r.json()["rejection_reason"]


async def test_superhuman_split_rejected(client, headers, athlete):
    run = _valid_run(9.5, reaction_ms=120)
    # 9.5s total with one 0.5s 10m segment (~20 m/s) — beyond any human.
    run["splits"] = [0.5] + [round((9.5 - 0.12 - 0.5) / 9, 4)] * 9
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    assert r.json()["accepted"] is False
    assert "impossible speed" in r.json()["rejection_reason"]


async def test_attribute_ceiling_blocks_a_rookie_world_record(
    client, headers, athlete
):
    """A fresh athlete (all attributes 40) cannot run 9.6 — that result is
    beyond the ceiling and must be rejected, not celebrated."""
    run = _valid_run(9.6, reaction_ms=150)
    r = await client.post("/api/v1/career/results", json=run, headers=headers)
    body = r.json()
    assert body["accepted"] is False
    assert "attribute ceiling" in body["rejection_reason"]

    # The same athlete CAN run a plausible 12.0.
    ok = await client.post(
        "/api/v1/career/results", json=_valid_run(12.0), headers=headers
    )
    assert ok.json()["accepted"] is True


def test_ceiling_model_shape():
    event = EVENTS["sprint-100m"]
    fresh = attribute_ceiling(event, dict.fromkeys(event.governing_attributes, 40.0))
    maxed = attribute_ceiling(event, dict.fromkeys(event.governing_attributes, 100.0))
    zero = attribute_ceiling(event, {})
    assert maxed < fresh < zero
    assert maxed == pytest.approx(9.55)
    assert zero == pytest.approx(13.5)


async def test_result_rate_limit_is_per_athlete(client, headers, athlete):
    codes = []
    for i in range(12):
        r = await client.post(
            "/api/v1/career/results", json=_valid_run(12.5 + i * 0.01), headers=headers
        )
        codes.append(r.json()["accepted"])
    assert False in codes, "the 11th+ result in a minute must be rejected"
    rejected = [
        c for c in codes if c is False
    ]
    assert len(rejected) >= 2


async def test_unknown_event_404s(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "teleport-5000m", "value_num": 1.0},
        headers=headers,
    )
    assert r.status_code == 404


def test_stage_progression_thresholds():
    assert stage_for_xp(0) == "rookie"
    assert stage_for_xp(499) == "rookie"
    assert stage_for_xp(500) == "regional"
    assert stage_for_xp(4000) == "elite"
    assert stage_for_xp(50_000) == "legend"


# ---- leaderboard ---------------------------------------------------------


async def test_leaderboard_is_validated_only_and_direction_aware(
    client, headers, athlete
):
    await client.post("/api/v1/career/results", json=_valid_run(12.8), headers=headers)
    await client.post("/api/v1/career/results", json=_valid_run(12.2), headers=headers)
    # An invalid (too fast) result that must NOT appear.
    await client.post(
        "/api/v1/career/results",
        json={"event": "sprint-100m", "value_num": 9.1, "reaction_ms": 150},
        headers=headers,
    )

    rival_headers = await _login(client, RIVAL)
    await client.post(
        "/api/v1/career/athlete",
        json={"name": "Vera Swift", "gender": "F"},
        headers=rival_headers,
    )
    await client.post(
        "/api/v1/career/results", json=_valid_run(12.5), headers=rival_headers
    )

    board = (
        await client.get(
            "/api/v1/career/leaderboard", params={"event": "sprint-100m"}
        )
    ).json()
    assert board["lower_is_better"] is True
    names = [row["athlete_name"] for row in board["rows"]]
    values = [row["value_num"] for row in board["rows"]]
    assert names == ["Kip Rono", "Vera Swift"], "best per athlete, fastest first"
    assert values == [12.2, 12.5], "the rejected 9.1 must not appear"
    assert [row["rank"] for row in board["rows"]] == [1, 2]


async def test_leaderboard_country_scope(client, headers, athlete):
    await client.post("/api/v1/career/results", json=_valid_run(12.4), headers=headers)

    board = (
        await client.get(
            "/api/v1/career/leaderboard",
            params={"event": "sprint-100m", "scope": "country", "country": "KEN"},
        )
    ).json()
    assert [row["athlete_name"] for row in board["rows"]] == ["Kip Rono"]

    empty = (
        await client.get(
            "/api/v1/career/leaderboard",
            params={"event": "sprint-100m", "scope": "country", "country": "JPN"},
        )
    ).json()
    assert empty["rows"] == []

    r = await client.get(
        "/api/v1/career/leaderboard",
        params={"event": "sprint-100m", "scope": "country"},
    )
    assert r.status_code == 422


async def test_leaderboard_unknown_event_404s(client):
    r = await client.get("/api/v1/career/leaderboard", params={"event": "nope"})
    assert r.status_code == 404


# ---- cloud save ----------------------------------------------------------


async def test_save_roundtrip_and_versioning(client, headers):
    fresh = (await client.get("/api/v1/career/save", headers=headers)).json()
    assert fresh == {"payload": {}, "version": 0, "updated_at": None}

    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 100, "unlocks": ["shoes-1"]}, "base_version": 0},
        headers=headers,
    )
    assert r.status_code == 200
    assert r.json()["version"] == 1

    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 150, "unlocks": ["shoes-1"]}, "base_version": 1},
        headers=headers,
    )
    assert r.json()["version"] == 2


async def test_stale_save_conflicts_with_merge_suggestion(client, headers):
    await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 200, "unlocks": ["a"]}, "base_version": 0},
        headers=headers,
    )

    # A second device writes from the stale version 0.
    r = await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 150, "unlocks": ["b"]}, "base_version": 0},
        headers=headers,
    )
    assert r.status_code == 409
    detail = r.json()["detail"]
    assert detail["server"]["version"] == 1
    assert detail["server"]["payload"]["total_xp"] == 200
    # The merge keeps BOTH devices' progress: max XP, union of unlocks.
    assert detail["suggested_merge"]["total_xp"] == 200
    assert detail["suggested_merge"]["unlocks"] == ["a", "b"]


def test_merge_is_additive_for_monotonic_data():
    ours = {"total_xp": 500, "unlocks": ["a", "b"], "name": "Kip"}
    theirs = {"total_xp": 350, "unlocks": ["b", "c"], "name": "Kipchoge"}
    merged = merge_saves(ours, theirs)
    assert merged["total_xp"] == 500, "XP never goes backwards"
    assert merged["unlocks"] == ["a", "b", "c"], "unlocks are never lost"
    assert merged["name"] == "Kipchoge", "non-monotonic keys take the newer write"


async def test_saves_are_per_user(client, headers):
    await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 999}, "base_version": 0},
        headers=headers,
    )
    other_headers = await _login(client, RIVAL)
    other = (await client.get("/api/v1/career/save", headers=other_headers)).json()
    assert other["payload"] == {}, "one user must never see another's save"
