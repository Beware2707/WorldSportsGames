"""Regressions for defects found by the final platform review.

Each test here failed against the code as originally written.
"""

import pytest
from sqlalchemy import select

from app.core.middleware import RateLimitMiddleware
from app.models import Notification
from app.services.ai import improves_downward
from app.services.notifications import notify

CREDS = {"email": "reg@example.com", "password": "regression-1", "display_name": "Reg"}


async def _login(client, creds=CREDS) -> dict:
    await client.post("/api/v1/auth/register", json=creds)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": creds["email"], "password": creds["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


# ---- rate limiter -------------------------------------------------------


async def test_rotating_x_forwarded_for_cannot_bypass_the_limiter(client, test_app):
    """XFF is attacker-controlled unless the peer is a trusted proxy.

    Honouring it unconditionally let anyone defeat the limit by rotating the
    header — worse than no limit, because it looked protected.
    """
    limit = next(
        m.kwargs["limit"]
        for m in test_app.user_middleware
        if m.cls is RateLimitMiddleware
    )

    codes = []
    for i in range(limit + 8):
        r = await client.post(
            "/api/v1/auth/login",
            data={"username": "nobody@example.com", "password": "wrong"},
            headers={"X-Forwarded-For": f"10.0.0.{i}"},
        )
        codes.append(r.status_code)

    assert 429 in codes, f"spoofed X-Forwarded-For bypassed the limiter: {codes}"


async def test_appended_forwarded_chain_also_cannot_bypass(client):
    codes = []
    for i in range(20):
        r = await client.post(
            "/api/v1/auth/login",
            data={"username": "nobody@example.com", "password": "wrong"},
            headers={"X-Forwarded-For": f"10.1.0.{i}, 203.0.113.9"},
        )
        codes.append(r.status_code)
    assert 429 in codes, f"appended chain bypassed the limiter: {codes}"


def test_limiter_evicts_expired_windows():
    """The key map must not grow forever on a long-lived process."""
    limiter = RateLimitMiddleware(
        app=None, limit=5, window_seconds=1, paths=("/x",), max_tracked_clients=2
    )
    limiter._hits["a"].append(0.0)
    limiter._hits["b"].append(0.0)
    limiter._evict_stale(now=100.0)
    assert limiter._hits == {}


# ---- notification fan-out ----------------------------------------------


async def test_dedupe_collision_does_not_destroy_other_notifications(
    client, db_sessionmaker
):
    """A duplicate for ONE recipient must undo only that insert.

    A bare session.rollback() discarded every notification already created in
    the transaction, so an unrelated user was silently never notified.
    """
    headers = await _login(client)
    first_id = (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]

    other = {"email": "reg2@example.com", "password": "regression-2", "display_name": "Reg2"}
    other_headers = await _login(client, other)
    second_id = (await client.get("/api/v1/auth/me", headers=other_headers)).json()["id"]

    async with db_sessionmaker() as session:
        # Pre-existing row for the SECOND user only.
        await notify(
            session,
            user_ids=[second_id],
            kind="breaking_news",
            title="First",
            body="Already delivered",
            payload={},
            dedupe_key="shared-key",
        )
        await session.commit()

    async with db_sessionmaker() as session:
        created = await notify(
            session,
            user_ids=[first_id, second_id],
            kind="breaking_news",
            title="Second",
            body="Fan-out",
            payload={},
            dedupe_key="shared-key",
        )
        await session.commit()

    # user one is new for this key, user two collides — only two survives...
    assert [n.user_id for n in created] == [first_id]

    async with db_sessionmaker() as session:
        rows = (await session.execute(select(Notification))).scalars().all()
    owners = sorted(n.user_id for n in rows)
    assert owners == sorted([first_id, second_id]), (
        "the collision destroyed a row it should not have touched"
    )

    # ...and every returned object corresponds to a row that actually exists,
    # so a push transport cannot deliver a phantom notification.
    persisted_ids = {n.id for n in rows}
    assert all(n.id in persisted_ids for n in created)


async def test_fan_out_is_not_n_plus_one(client, db_sessionmaker):
    """Preferences are read in one query, not one per recipient."""
    from sqlalchemy import event as sa_event

    headers = await _login(client)
    user_id = (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]

    async with db_sessionmaker() as session:
        statements: list[str] = []

        def record(conn, cursor, statement, *args):
            statements.append(statement)

        bind = session.get_bind()
        sync_engine = getattr(bind, "sync_engine", bind)
        sa_event.listen(sync_engine, "before_cursor_execute", record)
        try:
            await notify(
                session,
                user_ids=[user_id] * 1 + list(range(9000, 9020)),
                kind="breaking_news",
                title="Broadcast",
                body="To many",
                payload={},
                dedupe_key="broadcast-1",
            )
        finally:
            sa_event.remove(sync_engine, "before_cursor_execute", record)

    preference_reads = [s for s in statements if "notification_preference" in s]
    assert len(preference_reads) == 1, (
        f"expected one batched preference query, got {len(preference_reads)}"
    )


async def test_opting_out_still_works_with_the_batched_query(client, db_sessionmaker):
    headers = await _login(client)
    user_id = (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]
    await client.put(
        "/api/v1/users/me/notifications",
        json={"preferences": [{"kind": "breaking_news", "enabled": False}]},
        headers=headers,
    )

    async with db_sessionmaker() as session:
        created = await notify(
            session,
            user_ids=[user_id],
            kind="breaking_news",
            title="News",
            body="Body",
            payload={},
            dedupe_key="opt-out-check",
        )
        await session.commit()
    assert created == []


# ---- AI correctness ------------------------------------------------------


def test_lower_is_better_is_not_time_only():
    """Golf strokes and equestrian penalties also improve downward."""
    assert improves_downward("time") is True
    assert improves_downward("strokes") is True
    assert improves_downward("penalties") is True
    assert improves_downward("points") is False
    assert improves_downward("distance") is False
    assert improves_downward(None) is False


async def test_a_flat_series_is_steady_not_declining(client):
    from app.ai.base import InsightKind
    from app.ai.deterministic import DeterministicProvider

    insight = await DeterministicProvider().generate(
        InsightKind.PERFORMANCE_TREND,
        {"values": [10.5, 10.5, 10.5, 10.5], "lower_is_better": True},
    )
    assert "steady" in insight.text
    assert "declining" not in insight.text


async def test_a_tie_is_not_awarded_to_the_second_athlete(client):
    from app.ai.base import InsightKind
    from app.ai.deterministic import DeterministicProvider

    insight = await DeterministicProvider().generate(
        InsightKind.HEAD_TO_HEAD,
        {
            "a": {"name": "A"},
            "b": {"name": "B"},
            # Two meetings, neither decided.
            "meetings": [{"winner": None}, {"winner": None}],
        },
    )
    assert "level at 0–0" in insight.text
    assert "B leads" not in insight.text
    # The undecided meetings are stated, so the tally reconciles.
    assert "2 meeting(s) had no decisive placing" in insight.text


async def test_summary_does_not_count_disqualifications_as_podiums(client):
    """Harper Quinlan DNF'd the Paris 100m final; that is not a podium."""
    r = await client.get("/api/v1/ai/athletes/harper-quinlan/summary")
    text = r.json()["text"]
    # She has a genuine 2nd in the 200m freestyle, so exactly one podium.
    assert "1 podium finish(es)" in text, text


async def test_free_text_cannot_be_echoed_as_a_generated_insight(client):
    """The old /explain/result took the values as query params, so a caller
    could author text and have the platform attach a disclaimer vouching that
    it came from recorded results."""
    r = await client.get(
        "/api/v1/ai/explain/result",
        params={"value_text": "ATHLETE X DOPED", "position": 1},
    )
    assert r.status_code == 404, "the free-text explain endpoint must be gone"

    # The replacement accepts only a closed vocabulary.
    ok = await client.get("/api/v1/ai/explain/status", params={"code": "DNF"})
    assert ok.status_code == 200
    assert "Did Not Finish" in ok.json()["text"]

    bad = await client.get(
        "/api/v1/ai/explain/status", params={"code": "ATHLETE X DOPED"}
    )
    assert bad.status_code == 422


async def test_result_explanation_is_keyed_on_stored_records(client):
    events = (await client.get("/api/v1/events", params={"size": 50})).json()
    final = next(e for e in events["items"] if e["name"] == "Women's 100m Final")

    r = await client.get(
        f"/api/v1/ai/events/{final['id']}/results/zellie-dunbar/explain"
    )
    assert r.status_code == 200
    assert "position 1" in r.json()["text"]

    missing = await client.get(
        f"/api/v1/ai/events/{final['id']}/results/kiptoo-cherop/explain"
    )
    assert missing.status_code == 404


async def test_event_preview_does_not_query_per_entrant(client, db_sessionmaker):
    """A public endpoint must not issue N round-trips for an optional field."""
    from sqlalchemy import event as sa_event

    from app.repositories.event import get_event_detail
    from app.services.ai import event_context

    events = (await client.get("/api/v1/events", params={"size": 50})).json()
    marathon = next(e for e in events["items"] if e["name"] == "Men's Marathon")

    async with db_sessionmaker() as session:
        event = await get_event_detail(session, marathon["id"])
        statements: list[str] = []

        def record(conn, cursor, statement, *args):
            statements.append(statement)

        bind = session.get_bind()
        sync_engine = getattr(bind, "sync_engine", bind)
        sa_event.listen(sync_engine, "before_cursor_execute", record)
        try:
            context = await event_context(session, event)
        finally:
            sa_event.remove(sync_engine, "before_cursor_execute", record)

    assert len(context["entrants"]) == 3
    ranking_queries = [s for s in statements if "FROM ranking" in s]
    assert len(ranking_queries) <= 1, (
        f"one query per entrant: {len(ranking_queries)} ranking queries"
    )


# ---- factual reporting ---------------------------------------------------


async def test_country_athlete_count_is_real_not_the_page_size(client):
    r = await client.get("/api/v1/countries/JAM")
    body = r.json()
    assert body["athlete_count"] == len(body["athletes"]), (
        "with fewer athletes than the page limit these must agree"
    )

    listing = await client.get("/api/v1/athletes", params={"country": "JAM", "size": 100})
    assert body["athlete_count"] == listing.json()["total"]


# ---- games ---------------------------------------------------------------


async def test_a_racing_first_play_is_retried_not_lost(client, db_sessionmaker):
    """Two first plays racing to create the progress row must both record.

    The conflict surfaces at COMMIT (separate transactions), so the endpoint
    retries rather than 500-ing and discarding the play.
    """
    from app.models import UserProgress

    headers = await _login(client)
    user_id = (await client.get("/api/v1/auth/me", headers=headers)).json()["id"]

    # Simulate the loser of the race: another transaction already committed
    # the progress row while this request was in flight.
    async with db_sessionmaker() as session:
        session.add(UserProgress(user_id=user_id))
        await session.commit()

    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": 250.0},
        headers=headers,
    )
    assert r.status_code == 201, "the play was lost instead of recorded"
    assert r.json()["xp_awarded"] > 0


@pytest.mark.parametrize("score", [200.0, 400.0])
async def test_submitting_still_works_after_the_progress_fix(client, score):
    headers = await _login(client)
    r = await client.post(
        "/api/v1/games/sprint-reaction/sessions",
        json={"score": score},
        headers=headers,
    )
    assert r.status_code == 201
    assert r.json()["total_xp"] > 0


# ---- rankings ------------------------------------------------------------


async def test_default_rankings_do_not_merge_discipline_ladders(client):
    """With no discipline filter, /rankings must NOT merge every discipline's
    ladder into one list — that produced three athletes all shown as rank 1.
    No discipline means the overall (discipline-less) ladder, which for
    athlete world_ranking honestly has no entries."""
    r = await client.get("/api/v1/rankings")
    ranks = [e["rank"] for e in r.json()["entries"]]
    assert len(ranks) == len(set(ranks)), f"duplicate ranks: {ranks}"

    # Discipline-scoped ladders are unchanged.
    r = await client.get("/api/v1/rankings", params={"discipline": "track-field"})
    assert [e["rank"] for e in r.json()["entries"]] == [1, 2, 3]

    # The overall country medal_count ladder (discipline_id NULL) still works.
    r = await client.get(
        "/api/v1/rankings", params={"scope": "country", "methodology": "medal_count"}
    )
    body = r.json()
    assert body["entries"], "the overall nations ladder must still resolve"
    assert body["entries"][0]["entity_name"] == "Jamaica"
    assert body["as_of"] == "2026-08-01"
