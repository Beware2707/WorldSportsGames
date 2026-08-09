async def _edition_id(client, comp_slug: str, label: str) -> int:
    r = await client.get(f"/api/v1/competitions/{comp_slug}")
    return next(e["id"] for e in r.json()["editions"] if e["label"] == label)


async def test_events_list_filters(client):
    paris = await _edition_id(client, "olympic-games", "Paris 2024")
    r = await client.get("/api/v1/events", params={"edition_id": paris, "size": 50})
    assert r.status_code == 200
    names = {e["name"] for e in r.json()["items"]}
    assert "Women's 100m Final" in names
    assert "Men's Marathon" in names
    assert "Women's 100m — Round 1" not in names  # LA28, different edition

    r = await client.get(
        "/api/v1/events",
        params={"edition_id": paris, "discipline": "swimming"},
    )
    assert [e["name"] for e in r.json()["items"]] == ["Women's 200m Freestyle Final"]

    r = await client.get("/api/v1/events", params={"status": "scheduled"})
    assert "Women's 100m — Round 1" in {e["name"] for e in r.json()["items"]}

    r = await client.get("/api/v1/events", params={"status": "bogus"})
    assert r.status_code == 422


async def test_event_detail_results_ordering_and_kinds(client):
    paris = await _edition_id(client, "olympic-games", "Paris 2024")
    r = await client.get("/api/v1/events", params={"edition_id": paris, "size": 50})
    sprint_final = next(
        e for e in r.json()["items"] if e["name"] == "Women's 100m Final"
    )

    r = await client.get(f"/api/v1/events/{sprint_final['id']}")
    assert r.status_code == 200
    body = r.json()
    assert body["competition_slug"] == "olympic-games"
    assert body["edition"]["label"] == "Paris 2024"

    results = body["results"]
    # Ranked athletes first by position; DNF (no position) last.
    assert [x["position"] for x in results] == [1, 2, None]
    assert results[0]["athlete"]["slug"] == "zellie-dunbar"
    assert results[0]["value_text"] == "10.71"
    assert results[0]["value_kind"] == "time"
    assert results[2]["status"] == "DNF"

    r = await client.get("/api/v1/events/999999")
    assert r.status_code == 404


async def test_points_kind_event(client):
    paris = await _edition_id(client, "olympic-games", "Paris 2024")
    r = await client.get(
        "/api/v1/events",
        params={"edition_id": paris, "discipline": "artistic-gymnastics"},
    )
    event_id = r.json()["items"][0]["id"]
    r = await client.get(f"/api/v1/events/{event_id}")
    top = r.json()["results"][0]
    assert top["value_kind"] == "points"
    assert top["value_text"] == "57.566"
