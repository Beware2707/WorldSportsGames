async def test_register_login_me_roundtrip(client):
    r = await client.post("/api/v1/auth/register", json={
        "email": "fan@example.com", "password": "s3cret-pass", "display_name": "Sports Fan",
    })
    assert r.status_code == 201, r.text
    assert r.json()["email"] == "fan@example.com"
    assert "hashed_password" not in r.json()

    r = await client.post("/api/v1/auth/register", json={
        "email": "fan@example.com", "password": "s3cret-pass", "display_name": "Dup",
    })
    assert r.status_code == 409

    r = await client.post("/api/v1/auth/login",
                          data={"username": "fan@example.com", "password": "s3cret-pass"})
    assert r.status_code == 200
    token = r.json()["access_token"]

    r = await client.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 200
    assert r.json()["display_name"] == "Sports Fan"


async def test_login_rejects_bad_password(client):
    await client.post("/api/v1/auth/register", json={
        "email": "a@example.com", "password": "correct-horse", "display_name": "A",
    })
    r = await client.post("/api/v1/auth/login",
                          data={"username": "a@example.com", "password": "wrong"})
    assert r.status_code == 401


async def test_me_rejects_garbage_token(client):
    r = await client.get("/api/v1/auth/me", headers={"Authorization": "Bearer nonsense"})
    assert r.status_code == 401
