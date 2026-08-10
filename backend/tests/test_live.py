from fastapi.testclient import TestClient

CREDENTIALS = {"email": "sim@example.com", "password": "simulator-pass", "display_name": "Sim"}


async def auth_headers(client) -> dict:
    await client.post("/api/v1/auth/register", json=CREDENTIALS)
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDENTIALS["email"], "password": CREDENTIALS["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


def sync_auth_headers(tc: TestClient) -> dict:
    tc.post("/api/v1/auth/register", json=CREDENTIALS)
    r = tc.post(
        "/api/v1/auth/login",
        data={"username": CREDENTIALS["email"], "password": CREDENTIALS["password"]},
    )
    return {"Authorization": f"Bearer {r.json()['access_token']}"}


async def test_live_empty_by_default(client):
    """Honesty: nothing is live unless a LiveEvent row genuinely exists."""
    r = await client.get("/api/v1/live")
    assert r.status_code == 200
    assert r.json() == []


async def test_home_live_section_empty_without_live_coverage(client):
    """The home LIVE section must be driven by real coverage, never by an
    edition merely being in progress (the Diamond League season is seeded
    'in_progress' and must NOT surface here)."""
    r = await client.get("/api/v1/home")
    sections = {s["kind"]: s for s in r.json()["sections"]}
    assert sections["live_now"]["items"] == []


async def test_dev_simulate_full_lifecycle_and_home_live_section(client):
    headers = await auth_headers(client)
    r = await client.post(
        "/api/v1/live/dev/simulate",
        params={"steps": 2, "interval": 0, "wait": True},
        headers=headers,
    )
    assert r.status_code == 202, r.text
    event_id = r.json()["event_id"]
    assert r.json()["status"] == "completed"

    # After the lifecycle the event is completed with synthetic results.
    r = await client.get(f"/api/v1/events/{event_id}")
    body = r.json()
    assert body["status"] == "completed"
    assert [x["position"] for x in body["results"]] == [1, 2]
    assert all(x["value_kind"] == "time" for x in body["results"])

    # Nothing remains live, on either surface.
    assert (await client.get("/api/v1/live")).json() == []
    r = await client.get("/api/v1/home")
    sections = {s["kind"]: s for s in r.json()["sections"]}
    assert sections["live_now"]["items"] == []


async def test_simulate_requires_authentication(client):
    r = await client.post("/api/v1/live/dev/simulate", params={"wait": True})
    assert r.status_code == 401


async def test_simulate_hidden_without_dev_flag(client, monkeypatch):
    """The dev gate resolves before auth, so production 404s even unauthenticated."""
    from app.core.config import get_settings

    monkeypatch.setattr(get_settings(), "enable_dev_fixtures", False)
    r = await client.post("/api/v1/live/dev/simulate")
    assert r.status_code == 404


def test_ws_snapshot_then_updates(test_app):
    """Connect WS, see empty snapshot, run the simulator, receive tagged diffs."""
    with TestClient(test_app) as tc:
        headers = sync_auth_headers(tc)
        with tc.websocket_connect("/api/v1/live/ws") as ws:
            snapshot = ws.receive_json()
            assert snapshot["type"] == "snapshot"
            assert snapshot["events"] == []

            r = tc.post(
                "/api/v1/live/dev/simulate",
                params={"steps": 1, "interval": 0, "wait": True},
                headers=headers,
            )
            assert r.status_code == 202, r.text

            kinds, payloads = [], []
            for _ in range(4):  # status(live), progress, results, status(completed)
                msg = ws.receive_json()
                assert msg["type"] == "update"
                kinds.append(msg["kind"])
                payloads.append(msg["payload"])
            assert kinds == ["status", "progress", "results", "status"]
            assert payloads[0]["status"] == "live"
            assert payloads[-1]["status"] == "completed"
            # Honesty: simulated frames are always attributable.
            assert all(p.get("source") == "dev-sim" for p in payloads)
            assert [s["position"] for s in payloads[2]["standings"]] == [1, 2]


def test_ws_client_unregistered_on_disconnect(test_app):
    """A closed socket must leave the hub, or every publish fans out to zombies."""
    hub = test_app.state.live_hub
    with TestClient(test_app) as tc:
        with tc.websocket_connect("/api/v1/live/ws") as ws:
            ws.receive_json()  # snapshot
            assert len(hub._clients) == 1
        # Give the server task a beat to run its finally block.
        for _ in range(20):
            if not hub._clients:
                break
            tc.get("/health")
    assert hub._clients == set()
