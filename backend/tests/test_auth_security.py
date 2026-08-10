"""Negative-path auth tests.

The happy path plus a malformed token is not enough: a malformed token fails
JWT *parsing*, so it stays green even if signature or expiry verification is
disabled. These tests mint structurally valid tokens that must still be
rejected.
"""

from datetime import UTC, datetime, timedelta

import jwt
import pytest
from sqlalchemy import select

from app.core.config import get_settings
from app.models import AppUser

CREDENTIALS = {"email": "sec@example.com", "password": "correct-horse-1", "display_name": "Sec"}


async def _register_and_login(client) -> tuple[int, str]:
    r = await client.post("/api/v1/auth/register", json=CREDENTIALS)
    user_id = r.json()["id"]
    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDENTIALS["email"], "password": CREDENTIALS["password"]},
    )
    return user_id, r.json()["access_token"]


def _mint(subject: str, *, secret: str, expires_in: timedelta) -> str:
    settings = get_settings()
    now = datetime.now(UTC)
    return jwt.encode(
        {"sub": subject, "iat": now, "exp": now + expires_in},
        secret,
        algorithm=settings.jwt_algorithm,
    )


async def test_valid_token_is_accepted(client):
    _, token = await _register_and_login(client)
    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 200


async def test_expired_token_rejected(client):
    user_id, _ = await _register_and_login(client)
    expired = _mint(
        str(user_id), secret=get_settings().jwt_secret, expires_in=timedelta(minutes=-5)
    )
    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {expired}"})
    assert r.status_code == 401


async def test_token_signed_with_wrong_secret_rejected(client):
    user_id, _ = await _register_and_login(client)
    forged = _mint(
        str(user_id), secret="an-entirely-different-secret-value", expires_in=timedelta(hours=1)
    )
    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {forged}"})
    assert r.status_code == 401


@pytest.mark.parametrize("subject", ["not-a-number", "999999"])
async def test_bad_or_unknown_subject_rejected(client, subject):
    token = _mint(subject, secret=get_settings().jwt_secret, expires_in=timedelta(hours=1))
    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 401


async def test_deactivated_user_rejected(client, db_sessionmaker):
    _, token = await _register_and_login(client)
    async with db_sessionmaker() as session:
        user = (
            await session.execute(select(AppUser).where(AppUser.email == CREDENTIALS["email"]))
        ).scalar_one()
        user.is_active = False
        await session.commit()

    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 401

    r = await client.post(
        "/api/v1/auth/login",
        data={"username": CREDENTIALS["email"], "password": CREDENTIALS["password"]},
    )
    assert r.status_code == 403


async def test_insecure_production_config_is_rejected():
    """The app must refuse to boot with the public dev secret outside debug."""
    from app.core.config import DEFAULT_DEV_JWT_SECRET, Settings

    with pytest.raises(ValueError, match="SPORTS_JWT_SECRET"):
        Settings(debug=False, jwt_secret=DEFAULT_DEV_JWT_SECRET)

    with pytest.raises(ValueError, match="SPORTS_ENABLE_DEV_FIXTURES"):
        Settings(
            debug=False,
            jwt_secret="a-real-secret-value-long-enough",
            enable_dev_fixtures=True,
        )

    # Debug builds may use the dev defaults.
    Settings(debug=True, jwt_secret=DEFAULT_DEV_JWT_SECRET, enable_dev_fixtures=True)
