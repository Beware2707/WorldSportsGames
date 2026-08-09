async def test_home_feed_sections(client):
    r = await client.get("/api/v1/home")
    assert r.status_code == 200
    sections = {s["kind"]: s for s in r.json()["sections"]}
    assert set(sections) == {"live_now", "up_next", "featured_competitions", "athlete_spotlight"}

    # live_now only reflects editions genuinely marked live in the DB
    # (the Diamond League 2026 season fixture) — never synthesized data.
    live_labels = {i["label"] for i in sections["live_now"]["items"]}
    assert live_labels == {"2026 Season"}

    up_next = sections["up_next"]["items"]
    assert any(i["label"] == "LA28" for i in up_next)
    assert all(i["status"] == "upcoming" for i in up_next)

    assert len(sections["athlete_spotlight"]["items"]) > 0


async def test_search_categorized(client):
    r = await client.get("/api/v1/search", params={"q": "ath"})
    assert r.status_code == 200
    body = r.json()
    assert any(s["code"] == "athletics" for s in body["sports"])

    r = await client.get("/api/v1/search", params={"q": "okafor"})
    assert [a["slug"] for a in r.json()["athletes"]] == ["amara-okafor"]

    r = await client.get("/api/v1/search", params={"q": "x"})
    assert r.status_code == 422  # minimum 2 characters


async def test_health(client):
    r = await client.get("/health")
    assert r.status_code == 200
