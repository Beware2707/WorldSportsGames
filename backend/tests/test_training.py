"""Training: the only path that raises an attribute, so the only path a
cheat could take to raise the ceiling a race is validated against."""

import pytest

CREDS = {"email": "coach@example.com", "password": "coach-pass-1",
         "display_name": "Coach"}


@pytest.fixture
async def headers(client) -> dict:
    await client.post("/api/v1/auth/register", json=CREDS)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDS["email"], "password": CREDS["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


@pytest.fixture
async def athlete(client, headers) -> dict:
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "Trainee", "gender": "X"},
        headers=headers,
    )
    return r.json()


async def attribute(client, headers, key: str) -> float:
    me = (await client.get("/api/v1/career/athlete", headers=headers)).json()
    return me["attributes"][key]


async def test_drill_catalogue_is_the_contract(client):
    drills = (await client.get("/api/v1/career/drills")).json()
    codes = {d["code"] for d in drills}
    assert "reaction-drill" in codes
    reaction = next(d for d in drills if d["code"] == "reaction-drill")
    assert reaction["attribute"] == "reaction"
    assert reaction["lower_is_better"] is True
    # A client must be able to tell which way "good" runs without guessing.
    for drill in drills:
        assert drill["best_metric"] != drill["worst_metric"]


async def test_training_raises_the_attribute_the_server_chose(client, headers, athlete):
    before = await attribute(client, headers, "reaction")
    r = await client.post(
        "/api/v1/career/training",
        json={"drill": "reaction-drill", "metric": 140.0},
        headers=headers,
    )
    assert r.status_code == 201
    body = r.json()
    assert body["accepted"] is True
    assert body["attribute"] == "reaction"
    assert body["attribute_gain"] > 0
    assert body["xp_awarded"] > 0
    # The stored attribute matches what the server reported, not what the
    # client hoped for.
    after = await attribute(client, headers, "reaction")
    assert after == pytest.approx(before + body["attribute_gain"], abs=1e-6)
    assert body["attribute_after"] == pytest.approx(after, abs=1e-6)


async def test_client_cannot_claim_a_gain(client, headers, athlete):
    """The request has no field for a gain — sending one changes nothing."""
    before = await attribute(client, headers, "reaction")
    r = await client.post(
        "/api/v1/career/training",
        json={
            "drill": "reaction-drill",
            "metric": 140.0,
            "attribute_gain": 50.0,   # ignored: not part of the contract
            "xp_awarded": 99999,
            "attribute_after": 100.0,
        },
        headers=headers,
    )
    assert r.status_code == 201
    body = r.json()
    assert body["attribute_gain"] < 1.0
    assert body["xp_awarded"] < 100
    after = await attribute(client, headers, "reaction")
    assert after < before + 1.0


async def test_impossible_metric_is_rejected_and_recorded(client, headers, athlete):
    before = await attribute(client, headers, "reaction")
    # 40ms is below the false-start floor: not a reaction to anything.
    r = await client.post(
        "/api/v1/career/training",
        json={"drill": "reaction-drill", "metric": 40.0},
        headers=headers,
    )
    assert r.status_code == 201
    body = r.json()
    assert body["accepted"] is False
    assert "plausible range" in body["rejection_reason"]
    assert body["attribute_gain"] == 0
    assert body["xp_awarded"] == 0
    assert await attribute(client, headers, "reaction") == before

    # NaN cannot slide past the bounds check by comparing False to everything.
    nan = await client.post(
        "/api/v1/career/training",
        content='{"drill": "reaction-drill", "metric": NaN}',
        headers={**headers, "Content-Type": "application/json"},
    )
    assert nan.status_code == 422
    assert await attribute(client, headers, "reaction") == before


async def test_gains_diminish_as_the_attribute_rises(client, headers, athlete):
    """A perfect session must be worth far less to a strong athlete —
    otherwise grinding, not playing well, is the way to win."""
    first = (
        await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 120.0},
            headers=headers,
        )
    ).json()

    # Push the attribute up, then measure the same perfect session again.
    for _ in range(12):
        await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 120.0},
            headers=headers,
        )
    later = (
        await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 120.0},
            headers=headers,
        )
    ).json()
    assert later["attribute_gain"] < first["attribute_gain"], (
        f"identical perfect session got no cheaper: {first['attribute_gain']} "
        f"then {later['attribute_gain']}"
    )


async def test_daily_cap_bounds_grinding(client, headers, athlete):
    """Unbounded training would let an autoclicker out-train a player."""
    total = 0.0
    for _ in range(60):
        r = await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 120.0},
            headers=headers,
        )
        body = r.json()
        total += body["attribute_gain"]
    # The cap is 4.0 points per attribute per rolling day.
    assert total <= 4.0 + 1e-6, f"daily cap breached: {total}"
    assert (await client.post(
        "/api/v1/career/training",
        json={"drill": "reaction-drill", "metric": 120.0},
        headers=headers,
    )).json()["daily_remaining"] == pytest.approx(0.0, abs=1e-6)


async def test_replayed_session_is_idempotent(client, headers, athlete):
    body = {"drill": "reaction-drill", "metric": 150.0, "client_ref": "drill-1"}
    first = (await client.post("/api/v1/career/training", json=body, headers=headers)).json()
    replay = (await client.post("/api/v1/career/training", json=body, headers=headers)).json()

    assert replay["attribute_gain"] == first["attribute_gain"]
    assert replay["xp_awarded"] == first["xp_awarded"]
    # The tell: the attribute did not move a second time.
    assert replay["attribute_after"] == pytest.approx(first["attribute_after"], abs=1e-6)
    assert replay["total_xp"] == first["total_xp"]


async def test_unknown_drill_404s(client, headers, athlete):
    r = await client.post(
        "/api/v1/career/training",
        json={"drill": "steroid-injection", "metric": 100.0},
        headers=headers,
    )
    assert r.status_code == 404


async def test_training_requires_an_athlete(client, headers):
    r = await client.post(
        "/api/v1/career/training",
        json={"drill": "reaction-drill", "metric": 150.0},
        headers=headers,
    )
    assert r.status_code == 404


async def test_records_and_statistics_come_from_the_audit_trail(client, headers, athlete):
    # One valid run and one that the server rejects.
    good = {"event": "sprint-100m", "value_num": 12.4, "reaction_ms": 165,
            "splits": [round((12.4 - 0.165) / 10, 4)] * 10}
    await client.post("/api/v1/career/results", json=good, headers=headers)
    cheat = {"event": "sprint-100m", "value_num": 9.4, "reaction_ms": 165,
             "splits": [round((9.4 - 0.165) / 10, 4)] * 10}
    await client.post("/api/v1/career/results", json=cheat, headers=headers)

    records = (await client.get("/api/v1/career/records", headers=headers)).json()
    sprint = next(r for r in records if r["event"] == "sprint-100m")
    # The rejected 9.4 must NOT become a personal best.
    assert sprint["personal_best"] == pytest.approx(12.4, abs=1e-6)
    assert sprint["personal_best_text"] == "12.40"
    assert sprint["world_best"] == pytest.approx(12.4, abs=1e-6)
    assert sprint["world_best_holder"] == "Trainee"

    await client.post(
        "/api/v1/career/training",
        json={"drill": "reaction-drill", "metric": 150.0},
        headers=headers,
    )
    stats = (await client.get("/api/v1/career/statistics", headers=headers)).json()
    assert stats["races"] == 2
    assert stats["valid_races"] == 1
    # A rejected run is reported, not hidden.
    assert stats["rejected_races"] == 1
    assert stats["training_sessions"] == 1
    assert stats["total_distance_m"] == pytest.approx(100.0)
    assert stats["best_reaction_ms"] == pytest.approx(165.0)
    assert stats["career_stage"] == "rookie"


async def test_training_ceiling_still_binds_results(client, headers, athlete):
    """Training raises the ceiling — it must not remove it. A time beyond
    what the trained attributes permit is still refused."""
    for _ in range(40):
        await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 120.0},
            headers=headers,
        )
    r = await client.post(
        "/api/v1/career/results",
        json={"event": "sprint-100m", "value_num": 9.6, "reaction_ms": 130,
              "splits": [round((9.6 - 0.13) / 10, 4)] * 10},
        headers=headers,
    )
    body = r.json()
    assert body["accepted"] is False
    assert "ceiling" in body["rejection_reason"]


async def test_repeat_sessions_without_a_client_ref_are_each_recorded(
    client, headers, athlete
):
    # A session with no client_ref cannot be deduplicated, so two of them
    # are two sessions. The idempotency lookup must never run for a NULL
    # ref: it matches every previous NULL-ref row, and scalar_one_or_none()
    # then raises MultipleResultsFound instead of the real error.
    for _ in range(3):
        r = await client.post(
            "/api/v1/career/training",
            json={"drill": "reaction-drill", "metric": 180.0},
            headers=headers,
        )
        assert r.status_code == 201, r.text
        assert r.json()["accepted"] is True

    stats = await client.get("/api/v1/career/statistics", headers=headers)
    assert stats.status_code == 200, stats.text
    assert stats.json()["training_sessions"] == 3
