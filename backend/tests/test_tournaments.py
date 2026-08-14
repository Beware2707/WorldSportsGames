"""Tournament progression: server-owned fields and advancement."""

import pytest

from app.services.career import EVENTS
from app.services.tournament import ROUND_RULES, generate_field, position_in_field

RUNNER = {"email": "tourney@example.com", "password": "tourney-pass-1", "display_name": "T"}


async def _login(client, creds=RUNNER) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


@pytest.fixture
async def headers(client) -> dict:
    h = await _login(client)
    r = await client.post(
        "/api/v1/career/athlete",
        json={"name": "Tourney Runner", "gender": "F"},
        headers=h,
    )
    assert r.status_code == 201
    return h


def _run(time_s: float, reaction_ms: float = 160.0) -> dict:
    split = (time_s - reaction_ms / 1000.0) / 10
    return {
        "event": "sprint-100m",
        "value_num": time_s,
        "reaction_ms": reaction_ms,
        "splits": [round(split, 4)] * 10,
    }


def _beating_time(field: list[dict]) -> float:
    """A time that beats every opponent but stays within the rookie ceiling."""
    fastest = min(o["time"] for o in field)
    return max(round(fastest - 0.05, 2), 11.93)


# ---- unit: field generation ----------------------------------------------


def test_field_generation_is_deterministic():
    event = EVENTS["sprint-100m"]
    a = generate_field(7, "heat", event, 11.9, ["KEN", "JAM"])
    b = generate_field(7, "heat", event, 11.9, ["KEN", "JAM"])
    assert a == b, "same tournament + round must produce the same field"

    other = generate_field(8, "heat", event, 11.9, ["KEN", "JAM"])
    assert a != other, "different tournaments get different fields"


def test_field_sizes_match_round_rules():
    event = EVENTS["sprint-100m"]
    for round_name, (size, _) in ROUND_RULES.items():
        field = generate_field(1, round_name, event, 11.9, ["KEN"])
        assert len(field) == size
        assert all(
            event.min_plausible <= o["time"] <= event.max_plausible for o in field
        )


def test_position_ties_go_to_the_player():
    event = EVENTS["sprint-100m"]
    field = [{"name": "A", "iso3": None, "time": 12.0},
             {"name": "B", "iso3": None, "time": 12.5}]
    assert position_in_field(11.9, field, event) == 1
    assert position_in_field(12.0, field, event) == 1  # tie → player
    assert position_in_field(12.2, field, event) == 2
    assert position_in_field(13.0, field, event) == 3


# ---- API flow ------------------------------------------------------------


async def test_tournament_requires_an_athlete(client):
    h = await _login(client, {"email": "noath@example.com",
                              "password": "pass-word-1", "display_name": "N"})
    r = await client.post(
        "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=h
    )
    assert r.status_code == 404


async def test_start_generates_a_stored_qualification_field(client, headers):
    r = await client.post(
        "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["status"] == "in_progress"
    assert body["current_round"] == "qualification"
    field = body["rounds"][0]["field"]
    assert len(field) == 8
    # Re-reading returns the SAME field — it is stored, not regenerated.
    again = (
        await client.get(f"/api/v1/career/tournaments/{body['id']}", headers=headers)
    ).json()
    assert again["rounds"][0]["field"] == field


async def test_one_active_tournament_per_event(client, headers):
    await client.post(
        "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
    )
    r = await client.post(
        "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
    )
    assert r.status_code == 409


async def test_full_tournament_run_to_gold(client, headers):
    """Win every round; the server decides every advancement and the medal."""
    t = (
        await client.post(
            "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
        )
    ).json()

    expected_rounds = ["qualification", "heat", "semifinal", "final"]
    for round_name in expected_rounds:
        detail = (
            await client.get(f"/api/v1/career/tournaments/{t['id']}", headers=headers)
        ).json()
        assert detail["current_round"] == round_name
        field = next(
            r["field"] for r in detail["rounds"] if r["round"] == round_name
        )
        r = await client.post(
            f"/api/v1/career/tournaments/{t['id']}/results",
            json=_run(_beating_time(field)),
            headers=headers,
        )
        body = r.json()
        assert body["accepted"] is True, body
        assert body["position"] == 1

    final = (
        await client.get(f"/api/v1/career/tournaments/{t['id']}", headers=headers)
    ).json()
    assert final["status"] == "completed"
    assert final["final_position"] == 1

    # Gold pays a medal bonus on top of run XP.
    athlete = (await client.get("/api/v1/career/athlete", headers=headers)).json()
    assert athlete["total_xp"] >= 120


async def test_losing_a_round_eliminates(client, headers):
    t = (
        await client.post(
            "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
        )
    ).json()
    # A slow-but-valid run: slower than the whole field.
    slowest = max(o["time"] for o in t["rounds"][0]["field"])
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json=_run(round(slowest + 3.0, 2)),
        headers=headers,
    )
    body = r.json()
    assert body["accepted"] is True
    assert body["advanced"] is False
    assert body["tournament_status"] == "eliminated"

    # No further round may be played.
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json=_run(12.5),
        headers=headers,
    )
    assert r.status_code == 409


async def test_invalid_run_does_not_consume_the_round(client, headers):
    t = (
        await client.post(
            "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
        )
    ).json()
    # False start: rejected, round stays open.
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json={"event": "sprint-100m", "value_num": 12.5, "reaction_ms": 80},
        headers=headers,
    )
    body = r.json()
    assert body["accepted"] is False
    assert "false start" in body["rejection_reason"]

    detail = (
        await client.get(f"/api/v1/career/tournaments/{t['id']}", headers=headers)
    ).json()
    assert detail["status"] == "in_progress"
    assert detail["rounds"][0]["position"] is None, "round must still be open"

    # A valid retry works.
    field = detail["rounds"][0]["field"]
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json=_run(_beating_time(field)),
        headers=headers,
    )
    assert r.json()["accepted"] is True


async def test_wrong_event_result_rejected(client, headers):
    t = (
        await client.post(
            "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
        )
    ).json()
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json={"event": "swim-50m", "value_num": 30.0},
        headers=headers,
    )
    assert r.status_code == 422


async def test_tournaments_are_owner_scoped(client, headers):
    t = (
        await client.post(
            "/api/v1/career/tournaments", json={"event": "sprint-100m"}, headers=headers
        )
    ).json()

    other = await _login(
        client,
        {"email": "intruder@example.com", "password": "pass-word-2", "display_name": "I"},
    )
    await client.post(
        "/api/v1/career/athlete", json={"name": "Intruder", "gender": "M"}, headers=other
    )
    r = await client.get(f"/api/v1/career/tournaments/{t['id']}", headers=other)
    assert r.status_code == 404, "another player's tournament must be invisible"
    r = await client.post(
        f"/api/v1/career/tournaments/{t['id']}/results",
        json=_run(12.5),
        headers=other,
    )
    assert r.status_code == 404
