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

### Sprint 1 — Foundation (this sprint) ✅ scope
- Monorepo: `backend/` (FastAPI) + `app/` (Flutter) + `docker-compose.yml`.
- Backend: versioned `/api/v1`, auth (register/login/JWT), Sport/Discipline/Country/
  Athlete/Competition/CompetitionEdition models, Alembic migrations, pagination,
  filtering, OpenAPI, seed CLI with the full Olympic sport taxonomy + dev fixtures.
- Flutter: design system (tokens, dark/light), GoRouter + bottom navigation shell,
  Home shell with data-driven sections, Sports screen (real API), API client (Dio),
  Riverpod repositories with loading/error/empty states, honest placeholder tabs for
  Live/Games/Profile.
- Tests: backend API tests (pytest, SQLite in-memory), Flutter widget/unit tests.

### Sprint 2 — Live & Results
- Normalized `LiveEvent`/`LiveUpdate` model, WebSocket diff streaming, Redis pub/sub.
- Universal results model + sport-specific renderers. Competition detail pages.

### Sprint 3 — Personalization & Search
- Onboarding follows (sports/athletes/countries/competitions), personalized home feed,
  global search with categorized autocomplete, notification preferences.

### Sprint 4 — Rankings, Records, Countries
- Ranking methodologies per sport, records system, country profiles + medal tables.

### Sprint 5 — Games Engine
- Modular mini-game engine (reaction/timing/accuracy primitives), XP/levels/
  achievements, leaderboards (global/country/friends).

### Sprint 6 — AI & Media
- AI service abstraction (provider-agnostic), athlete summaries, event previews,
  H2H analysis; news/video adapters; push notifications.

### Sprint 7 — Hardening
- Observability, rate limiting, CDN/image pipeline, i18n, accessibility audit,
  load testing, CI/CD.

## 3. Definition of Done (every sprint)
- `ruff check` + `mypy` clean; `flutter analyze` clean.
- Backend + Flutter tests green; new logic covered.
- App runs end-to-end against dockerized Postgres.
- No secrets in git; env-driven config only.
