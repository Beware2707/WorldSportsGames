async def test_medal_table_is_gold_ordered_not_total_ordered(client):
    """Standard convention: gold first, then silver, then bronze — a country
    with more total medals but less gold must NOT outrank a gold winner."""
    r = await client.get("/api/v1/medals")
    assert r.status_code == 200
    rows = r.json()["rows"]
    assert rows, "expected a populated table"

    golds = [row["gold"] for row in rows]
    assert golds == sorted(golds, reverse=True), f"not gold-ordered: {golds}"

    # Ranks are dense positions starting at 1.
    assert [row["rank"] for row in rows] == list(range(1, len(rows) + 1))
    for row in rows:
        assert row["total"] == row["gold"] + row["silver"] + row["bronze"]


async def test_medal_table_filters_by_edition(client):
    editions = (await client.get("/api/v1/medals/editions")).json()
    labels = {e["label"] for e in editions}
    assert "Paris 2024" in labels and "Milano Cortina 2026" in labels

    winter = next(e for e in editions if e["label"] == "Milano Cortina 2026")
    r = await client.get("/api/v1/medals", params={"edition_id": winter["id"]})
    body = r.json()
    assert body["edition_label"] == "Milano Cortina 2026"
    iso3s = {row["country"]["iso3"] for row in body["rows"]}
    # Winter medallists only — no summer-only nation may appear.
    assert "NED" in iso3s
    assert "JAM" not in iso3s

    all_time = (await client.get("/api/v1/medals")).json()
    assert len(all_time["rows"]) > len(body["rows"])


async def test_medal_editions_only_lists_editions_with_data(client):
    """The filter must never offer an option that yields an empty table."""
    editions = (await client.get("/api/v1/medals/editions")).json()
    for edition in editions:
        rows = (
            await client.get("/api/v1/medals", params={"edition_id": edition["id"]})
        ).json()["rows"]
        assert rows, f"{edition['label']} was offered but has no medals"


async def test_records_filter_and_shape(client):
    r = await client.get("/api/v1/records", params={"kind": "WR"})
    assert r.status_code == 200
    records = r.json()
    assert records
    assert all(x["kind"] == "WR" for x in records)

    sprint = next(x for x in records if x["event_name"] == "100m")
    assert sprint["holder_name"] == "Zellie Dunbar"
    assert sprint["holder_slug"] == "zellie-dunbar"
    assert sprint["value_text"] == "10.54"
    assert sprint["country"]["iso3"] == "JAM"

    r = await client.get("/api/v1/records", params={"discipline": "marathon"})
    assert {x["discipline"]["code"] for x in r.json()} == {"marathon"}

    r = await client.get("/api/v1/records", params={"gender": "F"})
    assert all(x["gender"] == "F" for x in r.json())

    assert (await client.get("/api/v1/records", params={"kind": "XX"})).status_code == 422


async def test_records_cover_non_time_value_kinds(client):
    """Records must not assume every sport measures time."""
    r = await client.get("/api/v1/records", params={"discipline": "artistic-gymnastics"})
    record = r.json()[0]
    assert record["value_kind"] == "points"
    assert record["value_text"] == "59.211"
    assert record["unit"] == "pts"


async def test_rankings_ladder(client):
    r = await client.get(
        "/api/v1/rankings", params={"discipline": "track-field", "scope": "athlete"}
    )
    assert r.status_code == 200
    body = r.json()
    assert body["methodology"] == "world_ranking"
    assert body["as_of"] == "2026-08-01"
    entries = body["entries"]
    assert [e["rank"] for e in entries] == [1, 2, 3]
    assert entries[0]["entity_name"] == "Zellie Dunbar"
    assert entries[0]["entity_slug"] == "zellie-dunbar"
    assert entries[0]["entity_subtitle"] == "Jamaica"


async def test_rankings_support_a_different_methodology_and_scope(client):
    """Nothing assumes every ladder ranks athletes by world_ranking points."""
    r = await client.get(
        "/api/v1/rankings", params={"scope": "country", "methodology": "medal_count"}
    )
    body = r.json()
    assert body["scope"] == "country"
    assert body["entries"][0]["entity_name"] == "Jamaica"
    assert body["entries"][0]["entity_slug"] == "JAM"


async def test_unknown_ladder_is_empty_not_an_error(client):
    r = await client.get(
        "/api/v1/rankings", params={"discipline": "curling", "scope": "athlete"}
    )
    assert r.status_code == 200
    assert r.json()["entries"] == []

    assert (
        await client.get("/api/v1/rankings", params={"discipline": "nope"})
    ).status_code == 404
    assert (
        await client.get("/api/v1/rankings", params={"scope": "banana"})
    ).status_code == 422


async def test_country_profile(client):
    r = await client.get("/api/v1/countries/JAM")
    assert r.status_code == 200
    body = r.json()
    assert body["country"]["name"] == "Jamaica"
    assert body["medals"]["gold"] == 1
    assert [a["slug"] for a in body["athletes"]] == ["zellie-dunbar"]
    assert any(rec["kind"] == "WR" for rec in body["records"])

    # Case-insensitive, and unknown codes 404.
    assert (await client.get("/api/v1/countries/jam")).status_code == 200
    assert (await client.get("/api/v1/countries/ZZZ")).status_code == 404


async def test_country_profile_without_medals_still_renders(client):
    """A country with no medals must return a profile, not a broken section."""
    r = await client.get("/api/v1/countries/IND")
    assert r.status_code == 200
    body = r.json()
    assert body["medals"] is None
    assert body["athlete_count"] >= 1


async def test_athlete_profile_aggregates_everything(client):
    r = await client.get("/api/v1/athletes/zellie-dunbar/profile")
    assert r.status_code == 200
    body = r.json()

    assert body["athlete"]["full_name"] == "Zellie Dunbar"
    assert {d["code"] for d in body["disciplines"]} == {"track-field"}
    assert {rec["kind"] for rec in body["personal_bests"]} >= {"WR", "OR"}

    medals = body["medals"]
    assert medals[0]["metal"] == "gold"
    assert medals[0]["competition_name"] == "Olympic Games"
    assert medals[0]["edition_label"] == "Paris 2024"

    results = body["recent_results"]
    assert any(x["event_name"] == "Women's 100m Final" for x in results)
    final = next(x for x in results if x["event_name"] == "Women's 100m Final")
    assert final["position"] == 1
    assert final["value_text"] == "10.71"

    assert body["world_rankings"][0]["rank"] == 1


async def test_athlete_profile_sections_can_be_independently_empty(client):
    """An athlete with no medals/records/rankings still returns a profile."""
    r = await client.get("/api/v1/athletes/ishaan-verma/profile")
    assert r.status_code == 200
    body = r.json()
    assert body["athlete"]["full_name"] == "Ishaan Verma"
    assert body["medals"] == []
    assert body["personal_bests"] == []
    assert body["world_rankings"] == []

    assert (await client.get("/api/v1/athletes/nobody/profile")).status_code == 404
