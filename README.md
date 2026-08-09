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
cp .env.example .env            # then edit SPORTS_JWT_SECRET
docker compose up -d db redis   # Postgres on :5434, Redis on :6381

cd backend
python -m venv .venv && .venv/Scripts/activate   # Windows
pip install -e .[dev]
alembic upgrade head
python -m app.seed.run --fixtures   # taxonomy + reference + fictional dev athletes
uvicorn app.main:app --reload       # http://localhost:8000/docs
```

```bash
cd app
flutter pub get
flutter run   # Android emulator reaches the API via http://10.0.2.2:8000
```

## Data honesty

- The sport/discipline taxonomy and competition catalogue are reference data.
- Development athletes are **fictional** and double-gated
  (`--fixtures` flag + `SPORTS_ENABLE_DEV_FIXTURES=true`); their bios carry a
  `[DEV FIXTURE]` prefix.
- "Live Now" renders only editions whose status is genuinely `live` in the
  database. There is no simulated live data path anywhere.

## Tests & lint

```bash
cd backend && python -m pytest && python -m ruff check .
```

```bash
cd app && flutter analyze && flutter test
```
