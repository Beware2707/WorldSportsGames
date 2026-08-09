async def test_athletes_filter_by_sport(client):
    r = await client.get("/api/v1/athletes", params={"sport": "athletics", "size": 50})
    assert r.status_code == 200
    body = r.json()
    assert body["total"] >= 4
    slugs = {a["slug"] for a in body["items"]}
    assert "amara-okafor" in slugs
    assert "rafael-cintra" not in slugs  # footballer excluded


async def test_athletes_filter_by_country_and_query(client):
    r = await client.get("/api/v1/athletes", params={"country": "ken"})
    assert [a["slug"] for a in r.json()["items"]] == ["kiptoo-cherop"]

    r = await client.get("/api/v1/athletes", params={"q": "okaf"})
    assert r.json()["items"][0]["family_name"] == "Okafor"


async def test_athlete_detail_multi_discipline(client):
    r = await client.get("/api/v1/athletes/colette-marchand")
    assert r.status_code == 200
    body = r.json()
    assert body["country"]["iso3"] == "FRA"
    assert {d["code"] for d in body["disciplines"]} == {"road-cycling", "track-cycling"}
    # Dev fixtures must be visibly marked as fictional.
    assert body["bio"].startswith("[DEV FIXTURE")


async def test_competitions_and_detail(client):
    r = await client.get("/api/v1/competitions", params={"level": "olympic"})
    slugs = {c["slug"] for c in r.json()["items"]}
    assert {"olympic-games", "winter-olympic-games"} <= slugs

    r = await client.get("/api/v1/competitions/olympic-games")
    assert r.status_code == 200
    editions = r.json()["editions"]
    assert editions[0]["year"] >= editions[-1]["year"]  # newest first
    la28 = next(e for e in editions if e["label"] == "LA28")
    assert la28["status"] == "upcoming"
    assert la28["host_country"]["iso3"] == "USA"
