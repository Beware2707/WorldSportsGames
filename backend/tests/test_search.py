from app.core.text import like_pattern


def test_like_pattern_escapes_wildcards():
    assert like_pattern("ath") == "%ath%"
    assert like_pattern("100%") == "%100\\%%"
    assert like_pattern("a_b") == "%a\\_b%"
    assert like_pattern("back\\slash") == "%back\\\\slash%"


async def test_wildcard_query_does_not_match_everything(client):
    """'%' must be a literal, not "select all"."""
    everything = (await client.get("/api/v1/sports", params={"size": 100})).json()["total"]
    assert everything > 10

    r = await client.get("/api/v1/search", params={"q": "%%"})
    assert r.status_code == 200
    assert r.json()["sports"] == []

    r = await client.get("/api/v1/athletes", params={"q": "%"})
    assert r.json()["total"] == 0


async def test_underscore_query_is_literal(client):
    r = await client.get("/api/v1/search", params={"q": "__"})
    assert r.json()["athletes"] == []


async def test_suggest_ranks_prefix_matches_first(client):
    r = await client.get("/api/v1/search/suggest", params={"q": "ath"})
    assert r.status_code == 200
    items = r.json()["items"]
    assert items, "expected suggestions"
    assert items[0]["label"] == "Athletics"
    assert items[0]["kind"] == "sport"
    assert items[0]["slug"] == "athletics"


async def test_suggest_mixes_kinds_and_carries_navigation_slugs(client):
    r = await client.get("/api/v1/search/suggest", params={"q": "o"})
    items = r.json()["items"]
    kinds = {i["kind"] for i in items}
    assert kinds & {"sport", "athlete", "competition", "country"}
    for item in items:
        assert item["slug"], "every suggestion must be navigable"


async def test_suggest_requires_a_character(client):
    assert (await client.get("/api/v1/search/suggest", params={"q": ""})).status_code == 422


async def test_search_finds_athletes_by_either_name(client):
    r = await client.get("/api/v1/search", params={"q": "Kiptoo"})
    assert [a["slug"] for a in r.json()["athletes"]] == ["kiptoo-cherop"]

    r = await client.get("/api/v1/search", params={"q": "Cherop"})
    assert [a["slug"] for a in r.json()["athletes"]] == ["kiptoo-cherop"]


async def test_suggestions_use_display_labels_not_storage_enums(client):
    """Raw enum values like 'la28' must never reach the UI."""
    r = await client.get("/api/v1/search/suggest", params={"q": "cricket"})
    sport = next(i for i in r.json()["items"] if i["kind"] == "sport")
    assert sport["sublabel"] == "LA28 addition"

    r = await client.get("/api/v1/search/suggest", params={"q": "olympic games"})
    comp = next(i for i in r.json()["items"] if i["kind"] == "competition")
    assert comp["sublabel"] == "Olympic"


async def test_suggestions_keep_kinds_from_crowding_each_other_out(client):
    """A query matching many sports must still surface other kinds."""
    r = await client.get("/api/v1/search/suggest", params={"q": "a"})
    kinds = {i["kind"] for i in r.json()["items"]}
    assert len(kinds) > 1, f"only one kind survived the merge: {kinds}"


async def test_prefix_ranking_happens_in_sql_not_after_the_limit(client):
    """The best prefix match must win even when many rows contain the term.

    'ma' is contained in a dozen sport names; ranking a LIMITed sample in
    Python would let alphabetically-early contains-matches bury the true
    prefix match ("Marathon Swimming" is a discipline, "Modern Pentathlon"
    contains 'ma', etc.).
    """
    r = await client.get("/api/v1/search/suggest", params={"q": "ma"})
    labels = [i["label"] for i in r.json()["items"]]
    prefixed = [x for x in labels if x.lower().startswith("ma")]
    assert prefixed, f"no prefix match surfaced at all: {labels}"
    assert labels[0].lower().startswith("ma"), (
        f"a contains-match outranked a prefix match: {labels}"
    )
