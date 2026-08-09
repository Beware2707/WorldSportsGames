from fastapi.testclient import TestClient


async def test_live_empty_by_default(client):
    """Honesty: nothing is live unless a LiveEvent row genuinely exists."""
    r = await client.get("/api/v1/live")
    assert r.status_code == 200
    assert r.json() == []


async def test_dev_simulate_full_lifecycle(client):
    r = await client.post(
        "/api/v1/live/dev/simulate", params={"steps": 2, "interval": 0, "wait": True}
    )
    assert r.status_code == 202, r.text
    event_id = r.json()["event_id"]
    assert r.json()["status"] == "completed"

    # After the lifecycle the event is completed with synthetic results,
    # every one tagged as dev-sim in the update stream.
    r = await client.get(f"/api/v1/events/{event_id}")
    body = r.json()
    assert body["status"] == "completed"
    positions = [x["position"] for x in body["results"]]
    assert positions == [1, 2]
    assert all(x["value_kind"] == "time" for x in body["results"])

    # Nothing remains live.
    r = await client.get("/api/v1/live")
    assert r.json() == []


def test_ws_snapshot_then_updates(test_app):
    """Connect WS, see empty snapshot, run the simulator, receive tagged diffs."""
    with TestClient(test_app) as tc, tc.websocket_connect("/api/v1/live/ws") as ws:
        snapshot = ws.receive_json()
        assert snapshot["type"] == "snapshot"
        assert snapshot["events"] == []

        r = tc.post(
            "/api/v1/live/dev/simulate",
            params={"steps": 1, "interval": 0, "wait": True},
        )
        assert r.status_code == 202, r.text

        kinds = []
        payloads = []
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


async def test_simulate_hidden_without_dev_flag(client, monkeypatch):
    from app.core.config import get_settings

    settings = get_settings()
    monkeypatch.setattr(settings, "enable_dev_fixtures", False)
    r = await client.post("/api/v1/live/dev/simulate")
    assert r.status_code == 404
