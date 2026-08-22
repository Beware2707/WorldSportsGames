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


async def test_result_rate_limit_fires(client, headers, athlete):
    # Per-athlete scoping and the rejection reason are asserted in
    # test_career_review_fixes.py; here we only confirm the cap engages.
    codes = []
    for i in range(12):
        r = await client.post(
            "/api/v1/career/results", json=_valid_run(12.5 + i * 0.01), headers=headers
        )
        codes.append(r.json()["accepted"])
    assert codes.count(False) >= 2, "the 11th+ result in a minute must be rejected"


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
    # The union keys keep both devices' client-side collections.
    assert detail["suggested_merge"]["unlocks"] == ["a", "b"]
    # total_xp is NOT merged as progression. The save blob is client-written
    # scratch, so a "max" here would preserve whichever number a player typed
    # into their save file. Authoritative XP lives on the career athlete.
    assert detail["suggested_merge"]["total_xp"] == 150, (
        "client-written XP must be treated as ordinary data, not progression"
    )


def test_merge_is_additive_for_monotonic_data():
    ours = {"sessions_played": 500, "unlocks": ["a", "b"], "name": "Kip"}
    theirs = {"sessions_played": 350, "unlocks": ["b", "c"], "name": "Kipchoge"}
    merged = merge_saves(ours, theirs)
    assert merged["sessions_played"] == 500, "counters never go backwards"
    assert merged["unlocks"] == ["a", "b", "c"], "unlocks are never lost"
    assert merged["name"] == "Kipchoge", "non-monotonic keys take the newer write"


def test_merge_does_not_launder_client_written_xp():
    """The save blob is client-owned scratch, not progression.

    Merging it as monotonic made a number the player can type look
    authoritative: whichever save claimed more XP won, forever. Real XP only
    moves through validated results and validated training on the server.
    """
    ours = {"total_xp": 999999}
    theirs = {"total_xp": 10}
    assert merge_saves(ours, theirs)["total_xp"] == 10, (
        "an inflated save must not survive a merge as if it were earned"
    )


async def test_saves_are_per_user(client, headers):
    await client.put(
        "/api/v1/career/save",
        json={"payload": {"total_xp": 999}, "base_version": 0},
        headers=headers,
    )
    other_headers = await _login(client, RIVAL)
    other = (await client.get("/api/v1/career/save", headers=other_headers)).json()
    assert other["payload"] == {}, "one user must never see another's save"


# ---- distance events: higher is better -----------------------------------
#
# Every event until the long jump was a time, where LOWER wins. The ceiling
# check, the leaderboard aggregate, the personal-best comparison and the
# tournament ranking all branch on lower_is_better, and none of those
# branches had ever been executed. These tests execute them.


async def test_long_jump_ceiling_rejects_a_mark_that_is_too_FAR(
    client, headers, athlete, db_sessionmaker
):
    event = EVENTS["jump-long"]
    ceiling = attribute_ceiling(event, {k: 40.0 for k in event.governing_attributes})
    r = await client.post(
        "/api/v1/career/results",
        json={
            "event": "jump-long",
            "value_num": ceiling + 1.5,  # further than these attributes allow
            "wind": 0.4,
        },
        headers=headers,
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["accepted"] is False
    assert "ceiling" in body["rejection_reason"]

    # Stored for audit even though it counts for nothing.
    async with db_sessionmaker() as session:
        rows = (await session.execute(select(GameResult))).scalars().all()
    assert len(rows) == 1
    assert rows[0].event_code == "jump-long"
    assert rows[0].value_kind == "distance"
    assert rows[0].is_valid is False


async def test_long_jump_accepts_a_legal_mark_and_takes_the_FARTHEST_as_best(
    client, headers, athlete
):
    event = EVENTS["jump-long"]
    ceiling = attribute_ceiling(event, {k: 40.0 for k in event.governing_attributes})

    first = await client.post(
        "/api/v1/career/results",
        json={"event": "jump-long", "value_num": round(ceiling - 0.60, 2), "wind": 0.2},
        headers=headers,
    )
    assert first.status_code == 201, first.text
    assert first.json()["accepted"] is True
    assert first.json()["is_personal_best"] is True

    # A SHORTER jump is not an improvement, however much later it happened.
    shorter = await client.post(
        "/api/v1/career/results",
        json={"event": "jump-long", "value_num": round(ceiling - 0.90, 2), "wind": 0.2},
        headers=headers,
    )
    assert shorter.json()["accepted"] is True
    assert shorter.json()["is_personal_best"] is False

    # A longer one is.
    longer_value = round(ceiling - 0.30, 2)
    longer = await client.post(
        "/api/v1/career/results",
        json={"event": "jump-long", "value_num": longer_value, "wind": 0.2},
        headers=headers,
    )
    assert longer.json()["accepted"] is True
    assert longer.json()["is_personal_best"] is True

    # The leaderboard ranks the FARTHEST, not the smallest number.
    board = (
        await client.get(
            "/api/v1/career/leaderboard",
            params={"event": "jump-long", "scope": "global", "period": "all_time"},
        )
    ).json()
    assert board["rows"], board
    assert board["rows"][0]["value_text"] == f"{longer_value:.2f}"


async def test_long_jump_marks_keep_the_centimetre(client, headers, athlete):
    # A jump of 7.90 is "7.90", never "7.9": the trailing zero is the
    # difference between two marks a centimetre apart.
    from app.services.career import format_value

    assert format_value(EVENTS["jump-long"], 7.90) == "7.90"
    assert format_value(EVENTS["jump-long"], 8.0) == "8.00"


async def test_wind_limit_still_applies_to_the_long_jump(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "jump-long", "value_num": 4.20, "wind": 2.6},
        headers=headers,
    )
    assert r.json()["accepted"] is False
    assert "wind" in r.json()["rejection_reason"]


async def test_relay_needs_a_leg_split_for_every_leg(client, headers, athlete):
    # A relay's splits ARE its legs. Sending three says a leg was never
    # run, and the clock covering four of them would then be a time nobody
    # produced.
    event = EVENTS["relay-4x100"]
    ceiling = attribute_ceiling(event, {k: 40.0 for k in event.governing_attributes})
    legs = round(ceiling / 4.0, 3)

    short = await client.post(
        "/api/v1/career/results",
        json={
            "event": "relay-4x100",
            "value_num": round(ceiling + 0.5, 2),
            "reaction_ms": 180,
            "splits": [legs, legs, legs],
        },
        headers=headers,
    )
    assert short.status_code == 201, short.text
    assert short.json()["accepted"] is False
    assert "split" in short.json()["rejection_reason"]


async def test_relay_ceiling_rejects_a_team_too_fast_for_its_athlete(
    client, headers, athlete
):
    # The same rule as every timed event, and the reason a relay cannot be
    # the back door into a leaderboard: four legs of a rookie's speed do
    # not add up to a world record.
    event = EVENTS["relay-4x400"]
    ceiling = attribute_ceiling(event, {k: 40.0 for k in event.governing_attributes})
    too_fast = round(ceiling - 20.0, 2)
    legs = round(too_fast / 4.0, 3)

    r = await client.post(
        "/api/v1/career/results",
        json={
            "event": "relay-4x400",
            "value_num": too_fast,
            "reaction_ms": 190,
            "splits": [legs, legs, legs, legs],
        },
        headers=headers,
    )
    assert r.status_code == 201, r.text
    assert r.json()["accepted"] is False
    assert "ceiling" in r.json()["rejection_reason"]


async def test_relay_accepts_a_legal_team_time_and_formats_it_as_a_clock(
    client, headers, athlete
):
    event = EVENTS["relay-4x400"]
    ceiling = attribute_ceiling(event, {k: 40.0 for k in event.governing_attributes})
    total = round(ceiling + 4.0, 2)
    legs = round(total / 4.0, 3)

    r = await client.post(
        "/api/v1/career/results",
        json={
            "event": "relay-4x400",
            "value_num": total,
            "reaction_ms": 190,
            "splits": [legs, legs, legs, legs],
        },
        headers=headers,
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["accepted"] is True, body
    # Over a minute, so the sport writes it as minutes and seconds.
    assert ":" in body["value_text"]
