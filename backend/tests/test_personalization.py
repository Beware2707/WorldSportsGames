import pytest

CREDS = {"email": "fan@example.com", "password": "follow-me-1", "display_name": "Fan"}


@pytest.fixture
async def headers(client) -> dict:
    await client.post("/api/v1/auth/register", json=CREDS)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDS["email"], "password": CREDS["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


async def _sport_id(client, code: str) -> int:
    return (await client.get(f"/api/v1/sports/{code}")).json()["id"]


async def _athlete_id(client, slug: str) -> int:
    return (await client.get(f"/api/v1/athletes/{slug}")).json()["id"]


async def test_favorites_require_auth(client):
    assert (await client.get("/api/v1/users/me/favorites")).status_code == 401
    r = await client.post(
        "/api/v1/users/me/favorites", json={"entity_type": "sport", "entity_id": 1}
    )
    assert r.status_code == 401


async def test_follow_unfollow_is_idempotent(client, headers):
    sport_id = await _sport_id(client, "athletics")

    r = await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "sport", "entity_id": sport_id},
        headers=headers,
    )
    assert r.status_code == 201
    assert [(f["entity_type"], f["entity_id"]) for f in r.json()] == [("sport", sport_id)]
    assert r.json()[0]["name"] == "Athletics"

    # Following twice must not duplicate or error.
    r = await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "sport", "entity_id": sport_id},
        headers=headers,
    )
    assert r.status_code == 201
    assert len(r.json()) == 1

    r = await client.delete(
        f"/api/v1/users/me/favorites/sport/{sport_id}", headers=headers
    )
    assert r.status_code == 204
    assert (await client.get("/api/v1/users/me/favorites", headers=headers)).json() == []

    # Unfollowing again is also fine.
    r = await client.delete(
        f"/api/v1/users/me/favorites/sport/{sport_id}", headers=headers
    )
    assert r.status_code == 204


async def test_following_a_nonexistent_entity_is_rejected(client, headers):
    """A follow pointing at nothing would poison the personalized feed."""
    r = await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "athlete", "entity_id": 999999},
        headers=headers,
    )
    assert r.status_code == 404

    r = await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "not_a_thing", "entity_id": 1},
        headers=headers,
    )
    assert r.status_code == 422


async def test_bulk_set_favorites_rejects_unknown_ids_as_a_group(client, headers):
    sport_id = await _sport_id(client, "athletics")
    r = await client.put(
        "/api/v1/users/me/favorites",
        json={
            "favorites": [
                {"entity_type": "sport", "entity_id": sport_id},
                {"entity_type": "sport", "entity_id": 999999},
            ]
        },
        headers=headers,
    )
    assert r.status_code == 404
    # Nothing was written — a partial follow list is worse than a failed submit.
    assert (await client.get("/api/v1/users/me/favorites", headers=headers)).json() == []


async def test_onboarding_flow_sets_state(client, headers):
    state = (await client.get("/api/v1/users/me/onboarding", headers=headers)).json()
    assert state == {"completed": False, "follow_count": 0}

    sport_id = await _sport_id(client, "athletics")
    athlete_id = await _athlete_id(client, "amara-okafor")
    r = await client.put(
        "/api/v1/users/me/favorites",
        json={
            "favorites": [
                {"entity_type": "sport", "entity_id": sport_id},
                {"entity_type": "athlete", "entity_id": athlete_id},
            ]
        },
        headers=headers,
    )
    assert r.status_code == 200
    assert len(r.json()) == 2

    state = (await client.get("/api/v1/users/me/onboarding", headers=headers)).json()
    assert state == {"completed": True, "follow_count": 2}

    # Replacing the set removes what is no longer selected.
    r = await client.put(
        "/api/v1/users/me/favorites",
        json={"favorites": [{"entity_type": "sport", "entity_id": sport_id}]},
        headers=headers,
    )
    assert [(f["entity_type"], f["entity_id"]) for f in r.json()] == [("sport", sport_id)]


async def test_home_feed_is_personalized_only_when_following(client, headers):
    # Anonymous: generic feed, no "your" sections.
    anon = (await client.get("/api/v1/home")).json()
    assert not any(s["kind"].startswith("your_") for s in anon["sections"])

    # Signed in but following nothing: still generic.
    signed_in = (await client.get("/api/v1/home", headers=headers)).json()
    assert not any(s["kind"].startswith("your_") for s in signed_in["sections"])

    athlete_id = await _athlete_id(client, "kiptoo-cherop")
    sport_id = await _sport_id(client, "athletics")
    await client.put(
        "/api/v1/users/me/favorites",
        json={
            "favorites": [
                {"entity_type": "athlete", "entity_id": athlete_id},
                {"entity_type": "sport", "entity_id": sport_id},
            ]
        },
        headers=headers,
    )

    personalized = (await client.get("/api/v1/home", headers=headers)).json()
    sections = {s["kind"]: s for s in personalized["sections"]}
    assert "your_athletes" in sections
    assert "your_sports" in sections
    assert [a["slug"] for a in sections["your_athletes"]["items"]] == ["kiptoo-cherop"]
    assert [s["code"] for s in sections["your_sports"]["items"]] == ["athletics"]
    # Live section still leads and is still honest.
    assert personalized["sections"][0]["kind"] == "live_now"

    # An anonymous request is unaffected by another user's follows.
    anon_again = (await client.get("/api/v1/home")).json()
    assert not any(s["kind"].startswith("your_") for s in anon_again["sections"])


async def test_home_with_invalid_token_degrades_to_generic(client):
    """A stale token must not 401 a public endpoint."""
    r = await client.get("/api/v1/home", headers={"Authorization": "Bearer nonsense"})
    assert r.status_code == 200
    assert not any(s["kind"].startswith("your_") for s in r.json()["sections"])


async def test_following_a_country_surfaces_its_athletes(client, headers):
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    kenya = next(c for c in countries["items"] if c["iso3"] == "KEN")
    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "country", "entity_id": kenya["id"]},
        headers=headers,
    )
    sections = {
        s["kind"]: s for s in (await client.get("/api/v1/home", headers=headers)).json()["sections"]
    }
    assert [a["slug"] for a in sections["your_athletes"]["items"]] == ["kiptoo-cherop"]


async def test_notification_preferences_default_on_and_persist(client, headers):
    prefs = (await client.get("/api/v1/users/me/notifications", headers=headers)).json()
    assert prefs, "every known kind should be listed"
    assert all(p["enabled"] for p in prefs), "defaults are opt-out, not opt-in"

    r = await client.put(
        "/api/v1/users/me/notifications",
        json={"preferences": [{"kind": "breaking_news", "enabled": False}]},
        headers=headers,
    )
    assert r.status_code == 200
    updated = {p["kind"]: p["enabled"] for p in r.json()}
    assert updated["breaking_news"] is False
    assert updated["medal_result"] is True

    again = {
        p["kind"]: p["enabled"]
        for p in (await client.get("/api/v1/users/me/notifications", headers=headers)).json()
    }
    assert again["breaking_news"] is False


async def test_unknown_notification_kind_is_ignored(client, headers):
    r = await client.put(
        "/api/v1/users/me/notifications",
        json={"preferences": [{"kind": "not_a_kind", "enabled": False}]},
        headers=headers,
    )
    assert r.status_code == 200
    assert all(p["kind"] != "not_a_kind" for p in r.json())


async def test_follows_are_per_user(client, headers):
    sport_id = await _sport_id(client, "athletics")
    await client.post(
        "/api/v1/users/me/favorites",
        json={"entity_type": "sport", "entity_id": sport_id},
        headers=headers,
    )

    other = {"email": "other@example.com", "password": "other-pass-1", "display_name": "Other"}
    await client.post("/api/v1/auth/register", json=other)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": other["email"], "password": other["password"]},
    )
    other_headers = {"Authorization": f"Bearer {r.json()['access_token']}"}

    assert (await client.get("/api/v1/users/me/favorites", headers=other_headers)).json() == []


async def test_home_is_documented_as_public_in_openapi(client):
    """A generated client must not treat the public feed as auth-required."""
    spec = (await client.get("/openapi.json")).json()
    home = spec["paths"]["/api/v1/home"]["get"]
    assert "security" not in home, "personalized-but-public route must not require auth"

    # Routes that genuinely require auth still declare it.
    favorites = spec["paths"]["/api/v1/users/me/favorites"]["get"]
    assert favorites.get("security"), "follow routes must be documented as protected"


async def test_explicit_athlete_follows_outrank_country_derived_ones(client, headers):
    """A followed country must not crowd out deliberately followed athletes."""
    countries = (await client.get("/api/v1/countries", params={"size": 100})).json()
    usa = next(c for c in countries["items"] if c["iso3"] == "USA")
    ethiopia = next(c for c in countries["items"] if c["iso3"] == "ETH")
    worku = await _athlete_id(client, "tadesse-worku")  # ETH

    await client.put(
        "/api/v1/users/me/favorites",
        json={
            "favorites": [
                {"entity_type": "athlete", "entity_id": worku},
                {"entity_type": "country", "entity_id": usa["id"]},
                {"entity_type": "country", "entity_id": ethiopia["id"]},
            ]
        },
        headers=headers,
    )
    sections = {
        s["kind"]: s
        for s in (await client.get("/api/v1/home", headers=headers)).json()["sections"]
    }
    slugs = [a["slug"] for a in sections["your_athletes"]["items"]]
    assert slugs[0] == "tadesse-worku", (
        "the explicitly followed athlete must lead, not be sorted among "
        f"country-derived ones: {slugs}"
    )
