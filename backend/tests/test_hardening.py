"""Observability and abuse-resistance tests."""

import json
import logging

import pytest

from app.core.logging import JsonFormatter, redact

CREDS = {"email": "limit@example.com", "password": "limit-pass-11", "display_name": "Limit"}


def test_redaction_covers_nested_credentials():
    payload = {
        "Authorization": "Bearer abc.def.ghi",
        "user": {"password": "hunter2", "email": "a@example.com"},
        "headers": [{"X-Api-Key": "secret"}],
        "note": "called with Bearer eyJhbGciOi.payload.sig",
    }
    cleaned = redact(payload)

    assert cleaned["Authorization"] == "[REDACTED]"
    assert cleaned["user"]["password"] == "[REDACTED]"
    assert cleaned["user"]["email"] == "a@example.com", "not everything is a secret"
    assert cleaned["headers"][0]["X-Api-Key"] == "[REDACTED]"
    # A token embedded in free text is redacted too.
    assert "eyJhbGciOi" not in cleaned["note"]
    assert "[REDACTED]" in cleaned["note"]


def test_formatter_emits_one_json_object_per_line():
    record = logging.LogRecord(
        name="app.test",
        level=logging.INFO,
        pathname=__file__,
        lineno=1,
        msg="hello",
        args=(),
        exc_info=None,
    )
    record.context = {"path": "/api/v1/home", "token": "should-vanish"}

    line = JsonFormatter().format(record)
    assert "\n" not in line
    parsed = json.loads(line)
    assert parsed["level"] == "INFO"
    assert parsed["message"] == "hello"
    assert parsed["context"]["path"] == "/api/v1/home"
    assert parsed["context"]["token"] == "[REDACTED]"


async def test_every_response_carries_a_request_id(client):
    r = await client.get("/api/v1/sports", params={"size": 1})
    assert r.headers.get("X-Request-ID"), "responses must be correlatable"


async def test_supplied_request_id_is_echoed(client):
    r = await client.get(
        "/api/v1/sports", params={"size": 1}, headers={"X-Request-ID": "trace-me-123"}
    )
    assert r.headers["X-Request-ID"] == "trace-me-123"


async def test_login_attempts_are_rate_limited(client, test_app):
    """Unlimited password guessing is the risk; bcrypt cost alone is not a bound."""
    from app.core.middleware import RateLimitMiddleware

    limit = next(
        m.kwargs["limit"]
        for m in test_app.user_middleware
        if m.cls is RateLimitMiddleware
    )

    await client.post("/api/v1/auth/register", json=CREDS)

    statuses = []
    for _ in range(limit + 3):
        r = await client.post(
            "/api/v1/auth/login",
            data={"username": CREDS["email"], "password": "wrong-guess"},
        )
        statuses.append(r.status_code)

    assert 429 in statuses, f"never rate limited: {statuses}"
    first_429 = statuses.index(429)
    assert all(s == 401 for s in statuses[:first_429])

    blocked = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDS["email"], "password": "wrong-guess"},
    )
    assert blocked.status_code == 429
    assert blocked.headers.get("Retry-After")
    assert "Too many attempts" in blocked.json()["detail"]


async def test_rate_limit_does_not_touch_read_traffic(client):
    """Browsing must not be throttled by the credential limiter."""
    for _ in range(30):
        r = await client.get("/api/v1/sports", params={"size": 1})
        assert r.status_code == 200


@pytest.mark.parametrize("path", ["/api/v1/auth/login", "/api/v1/auth/register"])
async def test_limiter_covers_both_credential_endpoints(client, path):
    codes = set()
    for _ in range(20):
        if path.endswith("login"):
            r = await client.post(
                path, data={"username": "x@example.com", "password": "y"}
            )
        else:
            r = await client.post(
                path,
                json={
                    "email": "x@example.com",
                    "password": "password-1234",
                    "display_name": "X",
                },
            )
        codes.add(r.status_code)
    assert 429 in codes, f"{path} was not limited: {codes}"
