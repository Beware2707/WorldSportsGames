from datetime import date, timedelta

import pytest

from app.services.games import level_for_xp, next_streak, xp_progress

CREDS = {"email": "player@example.com", "password": "play-games-1", "display_name": "Player"}


@pytest.fixture
async def headers(client) -> dict:
    await client.post("/api/v1/auth/register", json=CREDS)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDS["email"], "password": CREDS["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


def test_levelling_curve_is_progressive():
    """Each level must cost more than the last, and level 1 starts at 0 XP."""
    assert level_for_xp(0) == 1
    assert level_for_xp(99) == 1
    assert level_for_xp(100) == 2
    assert level_for_xp(299) == 2
    assert level_for_xp(300) == 3

    thresholds = [
        next(xp for xp in range(0, 10_000) if level_for_xp(xp) == lvl)
        for lvl in range(1, 6)
    ]
    gaps = [b - a for a, b in zip(thresholds, thresholds[1:], strict=False)]
    assert gaps == sorted(gaps), f"levels must not get cheaper: {gaps}"

    level, into, needed = xp_progress(150)
    assert (level, into, needed) == (2, 50, 200)


def test_streaks_extend_hold_and_reset():
    today = date(2026, 8, 10)
    assert next_streak(None, today, 0) == 1
    assert next_streak(today - timedelta(days=1), today, 3) == 4
    # Same-day replay holds the streak rather than inflating it.
    assert next_streak(today, today, 3) == 3
    # A missed day resets, it does not continue.
    assert next_streak(today - timedelta(days=2), today, 9) == 1


async def test_game_catalogue_is_public_and_typed(client):
    r = await client.get("/api/v1/games")
    assert r.status_code == 200
    games = r.json()
    assert len(games) >= 8

    reaction = next(g for g in games if g["code"] == "sprint-reaction")
    assert reaction["engine"] == "reaction"
    assert reaction["score_direction"] == "lower_better"
    assert reaction["score_unit"] == "ms"
    assert reaction["config"]["rounds"] == 5
    assert reaction["personal_best"] is None

    accuracy = next(g for g in games if g["code"] == "free-throw")
    assert accuracy["score_direction"] == "higher_better"

    # Engines are a small closed set; games are data on top of them.
    assert {g["engine"] for g in games} <= {"reaction", "accuracy", "timing", "sequence"}


async def test_submitting_a_score_requires_auth(client):
    r = await client.post("/api/v1/games/sprint-reaction/sessions", json={"score": 250})
    assert r.status_code == 401


async def test_score_submission_awards_xp_and_tracks_personal_best(client, headers):
    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 300.0},
        headers=headers,
    )
    assert r.status_code == 201
    first = r.json()
    assert first["is_personal_best"] is True
    assert first["xp_awarded"] > 0
    assert first["level"] >= 1
    assert first["streak_days"] == 1
    assert any(a["code"] == "first-play" for a in first["unlocked"])

    # Lower is better here, so a slower time is NOT a personal best.
    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 400.0},
        headers=headers,
    )
    worse = r.json()
    assert worse["is_personal_best"] is False
    assert worse["total_xp"] > first["total_xp"], "every play still earns XP"

    # A faster time is.
    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 210.0},
        headers=headers,
    )
    assert r.json()["is_personal_best"] is True

    catalogue = (await client.get("/api/v1/games", headers=headers)).json()
    reaction = next(g for g in catalogue if g["code"] == "sprint-reaction")
    assert reaction["personal_best"] == 210.0, "best = fastest, not latest"


async def test_higher_better_game_uses_the_opposite_direction(client, headers):
    await client.post(
        "/api/v1/games/free-throw/sessions", json={"score": 7}, headers=headers
    )
    r = await client.post(
        "/api/v1/games/free-throw/sessions", json={"score": 4}, headers=headers
    )
    assert r.json()["is_personal_best"] is False

    r = await client.post(
        "/api/v1/games/free-throw/sessions", json={"score": 9}, headers=headers
    )
    assert r.json()["is_personal_best"] is True

    catalogue = (await client.get("/api/v1/games", headers=headers)).json()
    assert next(g for g in catalogue if g["code"] == "free-throw")["personal_best"] == 9


async def test_implausible_scores_are_rejected(client, headers):
    """A leaderboard is only as trustworthy as its worst row."""
    for score in (-5, 1, 999_999):
        r = await client.post(
            "/api/v1/games/sprint-reaction/sessions",
            json={"score": score},
            headers=headers,
        )
        assert r.status_code == 422, f"accepted implausible score {score}"


async def test_unknown_game_404s(client, headers):
    r = await client.post(
        "/api/v1/games/not-a-game/sessions", json={"score": 1}, headers=headers
    )
    assert r.status_code == 404


async def test_score_achievement_is_direction_aware(client, headers):
    """"Under 220ms" and "score 5 of 5" use the same trigger type."""
    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 205.0},
        headers=headers,
    )
    assert any(a["code"] == "quick-draw" for a in r.json()["unlocked"])

    r = await client.post(
        "/api/v1/games/penalty-shootout/sessions",
        json={"score": 5},
        headers=headers,
    )
    assert any(a["code"] == "perfect-penalties" for a in r.json()["unlocked"])


async def test_achievements_are_not_awarded_twice(client, headers):
    first = await client.post(
        "/api/v1/games/sprint-reaction/sessions", json={"score": 200}, headers=headers
    )
    assert any(a["code"] == "quick-draw" for a in first.json()["unlocked"])

    second = await client.post(
        "/api/v1/games/sprint-reaction/sessions", json={"score": 190}, headers=headers
    )
    assert not any(a["code"] == "quick-draw" for a in second.json()["unlocked"])

    progress = (await client.get("/api/v1/games/me/progress", headers=headers)).json()
    codes = [a["code"] for a in progress["achievements"]]
    assert codes.count("quick-draw") == 1


async def test_leaderboard_is_one_row_per_player_and_direction_ordered(client, headers):
    for score in (400.0, 250.0, 320.0):
        await client.post(
            "/api/v1/games/sprint-reaction/sessions",
            json={"score": score},
            headers=headers,
        )

    other = {"email": "rival@example.com", "password": "rival-pass-1", "display_name": "Rival"}
    await client.post("/api/v1/auth/register", json=other)
    token = (
        await client.post(
            "/api/v1/auth/login",
            data={"username": other["email"], "password": other["password"]},
        )
    ).json()["access_token"]
    await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 275.0},
        headers={"Authorization": f"Bearer {token}"},
    )

    board = (await client.get("/api/v1/games/sprint-reaction/leaderboard")).json()
    assert board["score_direction"] == "lower_better"
    names = [row["display_name"] for row in board["rows"]]
    assert names == ["Player", "Rival"], "fastest first, one row per player"
    assert [row["rank"] for row in board["rows"]] == [1, 2]
    assert board["rows"][0]["score"] == 250.0


async def test_higher_better_leaderboard_orders_descending(client, headers):
    await client.post(
        "/api/v1/games/free-throw/sessions", json={"score": 3}, headers=headers
    )
    board = (await client.get("/api/v1/games/free-throw/leaderboard")).json()
    assert board["score_direction"] == "higher_better"
    assert board["rows"][0]["score"] == 3


async def test_country_leaderboard_scopes_to_followers(client, headers):
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    kenya = next(c for c in countries["items"] if c["iso3"] == "KEN")

    await client.post(
        "/api/v1/games/sprint-reaction/sessions", json={"score": 260}, headers=headers
    )

    board = (
        await client.get(
            "/api/v1/games/sprint-reaction/leaderboard",
            params={"scope": "country", "country": "KEN"},
        )
    ).json()
    assert board["scope_label"] == "Kenya"
    assert board["rows"] == [], "not following Kenya yet"

    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "country", "entity_id": kenya["id"]},
        headers=headers,
    )
    board = (
        await client.get(
            "/api/v1/games/sprint-reaction/leaderboard",
            params={"scope": "country", "country": "KEN"},
        )
    ).json()
    assert [row["display_name"] for row in board["rows"]] == ["Player"]

    r = await client.get(
        "/api/v1/games/sprint-reaction/leaderboard", params={"scope": "country"}
    )
    assert r.status_code == 422, "country scope needs a country"


async def test_friends_leaderboard_includes_self_and_only_friends(client, headers):
    """The friends board is accepted friendships plus the caller.

    (It was honestly empty until the friendship graph existed; now it must
    show yourself and never show strangers.) Full friend flows are covered in
    test_friends.py.
    """
    await client.post(
        "/api/v1/games/sprint-reaction/sessions", json={"score": 260}, headers=headers
    )
    board = (
        await client.get(
            "/api/v1/games/sprint-reaction/leaderboard",
            params={"scope": "friends"},
            headers=headers,
        )
    ).json()
    assert [row["display_name"] for row in board["rows"]] == ["Player"], (
        "yourself on your own friends board; no strangers"
    )

    r = await client.get(
        "/api/v1/games/sprint-reaction/leaderboard", params={"scope": "friends"}
    )
    assert r.status_code == 401


async def test_daily_challenge_is_stable_and_shared(client):
    first = (await client.get("/api/v1/games/daily")).json()
    second = (await client.get("/api/v1/games/daily")).json()
    assert first["game"]["code"] == second["game"]["code"]
    assert first["date"] == second["date"]


async def test_progress_lists_locked_and_unlocked(client, headers):
    progress = (await client.get("/api/v1/games/me/progress", headers=headers)).json()
    assert progress["total_xp"] == 0
    assert progress["level"] == 1
    assert progress["achievements"] == []
    assert len(progress["locked_achievements"]) >= 8

    await client.post(
        "/api/v1/games/sprint-reaction/sessions", json={"score": 300}, headers=headers
    )
    progress = (await client.get("/api/v1/games/me/progress", headers=headers)).json()
    assert progress["total_xp"] > 0
    assert progress["sessions_played"] == 1
    unlocked = {a["code"] for a in progress["achievements"]}
    locked = {a["code"] for a in progress["locked_achievements"]}
    assert "first-play" in unlocked
    assert not (unlocked & locked), "an achievement cannot be both"
