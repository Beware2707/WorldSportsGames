async def test_sports_pagination_and_categories(client):
    r = await client.get("/api/v1/sports", params={"size": 10})
    assert r.status_code == 200
    body = r.json()
    assert body["total"] == 54  # full taxonomy: 32 summer + 6 LA28 + 16 winter
    assert len(body["items"]) == 10
    assert body["pages"] >= 6

    r = await client.get("/api/v1/sports", params={"category": "winter", "size": 100})
    winter = r.json()
    assert winter["total"] == 16
    assert all(s["category"] == "winter" for s in winter["items"])

    r = await client.get("/api/v1/sports", params={"category": "bogus"})
    assert r.status_code == 422  # whitelisted filter values only


async def test_sport_detail_includes_disciplines(client):
    r = await client.get("/api/v1/sports/athletics")
    assert r.status_code == 200
    names = {d["name"] for d in r.json()["disciplines"]}
    assert {"Track & Field", "Marathon", "Race Walking"} <= names

    r = await client.get("/api/v1/sports/quidditch")
    assert r.status_code == 404


async def test_single_discipline_sports_get_default_discipline(client):
    r = await client.get("/api/v1/sports/judo")
    assert r.status_code == 200
    assert [d["code"] for d in r.json()["disciplines"]] == ["judo"]


async def test_countries_sorted(client):
    r = await client.get("/api/v1/countries", params={"size": 100, "sort": "name"})
    names = [c["name"] for c in r.json()["items"]]
    assert names == sorted(names)
