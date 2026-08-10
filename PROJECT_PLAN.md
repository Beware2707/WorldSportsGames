# World Athletics & Sports Games — Project Plan

## 0. Repository Inspection Report

The working directory originally opened (`Downloads/Metro`) contains **MetroPulse**, a
production Delhi Metro GTFS platform — an unrelated product with uncommitted work in
progress. Per the owner's decision, this project is a **greenfield build in a sibling
directory** (`Downloads/WorldSportsGames`). Findings from the inspection:

| Question | Finding |
|---|---|
| A. Existing tech stack | MetroPulse: FastAPI + SQLAlchemy 2 (async) + Alembic + PostgreSQL + Redis backend; Flutter (Riverpod, GoRouter, Dio, Freezed, Hive) client. Unrelated product. |
| B. Existing folder structure | Clean Architecture (`domain/application/infrastructure/api`) backend; feature-based Flutter app. |
| C. Existing features | Transit-specific (GTFS ingestion, ETAs, live train positions). Nothing sports-related. |
| D. Existing dependencies | Proven on this machine: Python 3.14, Flutter 3.44, Docker 29. Version pins reused here. |
| E. Existing DB/backend | PostgreSQL + Alembic migrations — pattern reused, schema not. |
| F. Existing API integrations | DMRC GTFS-RT only. Not applicable. |
| G. Reusable | Architectural patterns only: async repository pattern, composition-root wiring, diff-based WebSocket fan-out design, honest-data discipline (never label estimates as live). |
| H. To refactor | N/A — greenfield. |
| I. Missing | Everything; built per the roadmap below. |

Answers to J–N are the dedicated documents: [ARCHITECTURE.md](ARCHITECTURE.md) (J, M),
[DATABASE.md](DATABASE.md) (K), [API.md](API.md) (L), and the roadmap below (N).

## 1. Product Summary

A multi-sport global platform covering all Summer, Winter and LA28 Olympic sports:
live center, results, rankings, records, athletes, countries, competitions, news,
interactive sports mini-games, and AI insights — designed sport-agnostic from day one.

**Non-negotiable principles**

1. No sport, competition, or provider is hard-coded into core architecture.
2. No fake live data ever reaches production paths. Development mock data lives behind
   a `DevMock*` repository implementation selected only by an explicit dev flag and is
   visually labelled "DEV DATA" in the app.
3. AI outputs are always labelled as predictions/estimates.
4. Provider adapters normalize external data before it touches domain models.

## 2. Phased Roadmap

### Sprint 1 — Foundation ✅ DONE
- Monorepo: `backend/` (FastAPI) + `app/` (Flutter) + `docker-compose.yml`.
- Backend: versioned `/api/v1`, auth (register/login/JWT), Sport/Discipline/Country/
  Athlete/Competition/CompetitionEdition models, Alembic migrations, pagination,
  filtering, OpenAPI, seed CLI with the full Olympic sport taxonomy + dev fixtures,
  Redis read-through cache on catalogue endpoints (fail-open), GitHub Actions CI.
- Flutter: design system (tokens, dark/light), GoRouter + bottom navigation shell,
  data-driven Home feed, Sports/Athletes/Competitions screens, API client (Dio),
  Riverpod repositories with loading/error/empty states, JWT persisted in secure
  storage with session restore.
- Tests: backend API tests (pytest, SQLite in-memory), Flutter widget/unit tests.

### Sprint 2 — Live & Results ✅ DONE
- Normalized `Event`/`Participation`/`Result`/`ResultDetail` +
  `LiveEvent`/`LiveUpdate` models; `/api/v1/events` + `/api/v1/live` REST.
- WebSocket diff streaming (`/api/v1/live/ws`) with Redis pub/sub fan-out across
  replicas, failing open to in-process broadcast; dev-gated live simulator
  (404 in prod, every frame tagged `source: dev-sim`).
- Flutter Live Center (WS diffs over a REST base, honest empty state),
  competition detail → edition schedule → event results, sport-agnostic
  result renderers (time/distance/points/score + DNS/DNF/DSQ + medal styling).
- Remaining for later sprints: real provider ingestion (live data today only
  comes from the dev simulator), result_detail rendering, venue model.

### Sprint 3 — Personalization & Search ✅ DONE
- Follows (`Favorite`) for sports/athletes/countries/competitions with CRUD +
  bulk-set, entity existence validated on every write, per-user isolation.
- Personalized home feed: `GET /api/v1/home` takes an *optional* token and
  inserts "Your Athletes / Your Sports / Your Competitions" after Live Now;
  anonymous and stale-token callers get the generic feed, never a 401.
- Onboarding: 4-step picker, every step skippable, submits the whole selection.
- Global search: categorized results plus ranked `/search/suggest` autocomplete
  (prefix matches first), LIKE wildcards escaped everywhere user input reaches
  `ilike` so `%` can no longer mean "match everything".
- Notification preferences: opt-out model (absence = enabled) with a settings
  screen. Delivery itself is Sprint 6 — the UI says so rather than implying
  notifications already send.

### Sprint 4 — Rankings, Records, Countries (next)

- Ranking methodologies per sport, records system, country profiles + medal tables.
- Athlete and country profile screens (search suggests them today but says
  "coming soon" rather than navigating somewhere misleading).

### Sprint 5 — Games Engine
- Modular mini-game engine (reaction/timing/accuracy primitives), XP/levels/
  achievements, leaderboards (global/country/friends).

### Sprint 6 — AI & Media ✅ DONE
- Provider-agnostic AI abstraction. `AIInsight` **cannot be constructed
  without a disclaimer**, and predictive kinds are forced to
  `is_estimate=True` in `__post_init__` — a caller cannot emit an unlabelled
  prediction even by passing `is_estimate=False`. The schema rejects a blank
  disclaimer, and the Flutter `InsightCard` is the only widget that renders
  generated text.
- Default provider is **deterministic**, not an LLM: it computes insights
  arithmetically from recorded results, so it cannot hallucinate a medal that
  was never won and needs no API key. An LLM provider is opt-in, is given the
  same pre-computed context, and any failure falls back to deterministic —
  an outage degrades quality, never correctness.
- Media links out to publishers (`source` + canonical `url`); nothing is
  scraped or rehosted.
- Notification delivery honours Sprint 3 preferences **before writing a row**,
  so an opted-out kind leaves no trace, and `dedupe_key` makes generation
  idempotent across retries.

### Sprint 7 — Hardening
- Observability, rate limiting, CDN/image pipeline, i18n, accessibility audit,
  load testing, CI/CD.

## 3. Definition of Done (every sprint)
- `ruff check` + `mypy` clean; `flutter analyze` clean.
- Backend + Flutter tests green; new logic covered.
- App runs end-to-end against dockerized Postgres.
- No secrets in git; env-driven config only.
