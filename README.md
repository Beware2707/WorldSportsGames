# World Athletics & Sports Games

Multi-sport global platform: live center, results, rankings, records, athletes,
countries, competitions, sports mini-games and AI insights — architected
sport-agnostic from day one.

Docs: [PROJECT_PLAN.md](PROJECT_PLAN.md) · [ARCHITECTURE.md](ARCHITECTURE.md) ·
[DATABASE.md](DATABASE.md) · [API.md](API.md)

## Layout

```
backend/   FastAPI + SQLAlchemy (async) + Alembic + PostgreSQL + Redis
app/       Flutter client (Riverpod · GoRouter · Dio)
```

## Quick start (development)

```bash
docker compose up -d db redis   # Postgres on :5434, Redis on :6381

cd backend
python -m venv .venv && .venv/Scripts/activate   # Windows
pip install -e .[dev]
cp ../.env.example .env          # uncomment the "Local development" block
alembic upgrade head
python -m app.seed.run --fixtures   # taxonomy + reference + fictional dev data
uvicorn app.main:app --reload       # http://localhost:8000/docs
```

`backend/.env` needs `SPORTS_DEBUG=true`, a `SPORTS_JWT_SECRET`, and
`SPORTS_ENABLE_DEV_FIXTURES=true` for demo data and the live simulator:

```bash
curl -X POST "http://localhost:8000/api/v1/live/dev/simulate?steps=5&interval=1.5" -H "Authorization: Bearer $TOKEN"
```

```bash
cd app
flutter pub get
flutter run   # Android emulator reaches the API via http://10.0.2.2:8000
```

## Data honesty

- The sport/discipline taxonomy and competition catalogue are reference data.
- Development athletes and events are **fictional** and double-gated
  (`--fixtures` flag + `SPORTS_ENABLE_DEV_FIXTURES=true`, which itself requires
  `SPORTS_DEBUG=true`); their bios carry a `[DEV FIXTURE]` prefix.
- A LIVE indicator anywhere in the product requires a real `LiveEvent` row.
  A competition edition that is merely running is `in_progress`, never `live`.
- If the live WebSocket drops, the app labels its data "LAST KNOWN" rather than
  continuing to present it as live.
- The backend refuses to start with the dev JWT secret or dev fixtures enabled
  while `SPORTS_DEBUG=false`.

See [ARCHITECTURE.md](ARCHITECTURE.md) — each invariant has a test that fails
if it is broken.

## Tests & lint

```bash
cd backend && python -m pytest && python -m ruff check .
```

```bash
cd app && flutter analyze && flutter test
```
