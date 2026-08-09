# Architecture

## 1. System Overview

```
                    ┌─────────────────────────────┐
 External providers │ SportsDataProvider adapters │  (Sprint 2+; interface exists now)
 (federation APIs)  │  normalize → domain model   │
                    └──────────────┬──────────────┘
                                   ▼
        ┌──────────────────────────────────────────────┐
        │ FastAPI backend (backend/app)                │
        │  api/v1 → services → repositories → models   │
        │  PostgreSQL (SoT)      Redis (cache/pubsub)  │
        └──────────────┬───────────────────────────────┘
                REST + WebSocket (versioned /api/v1)
                       ▼
        ┌──────────────────────────────────────────────┐
        │ Flutter app (app/lib)                        │
        │  features → repositories → ApiClient (Dio)   │
        │  Riverpod state · GoRouter navigation        │
        └──────────────────────────────────────────────┘
```

## 2. Backend layout (`backend/app/`)

```
api/            routers only — no business logic
  v1/           one module per resource; auth dependencies
core/           config (pydantic-settings), security (JWT, hashing), pagination
db/             async engine/session factory, Alembic env, base class
models/         SQLAlchemy 2.0 typed ORM models
schemas/        Pydantic v2 request/response models (never ORM leaks)
repositories/   data access, one per aggregate; async, no N+1
services/       use-case orchestration (auth, catalog, home feed)
providers/      SportsDataProvider protocol + adapters (GenericProvider now)
seed/           dev/demo fixtures — explicitly dev-only, guarded by env flag
workers/        (Sprint 2) background ingestion, Celery-compatible layout
websocket/      (Sprint 2) live hub, Redis pub/sub fan-out
```

Rules: routers depend on services, services on repositories, repositories on models.
Nothing imports upward. `wiring` happens via FastAPI dependency injection.

## 3. Provider architecture

`providers/base.py` defines the `SportsDataProvider` protocol:
`fetch_competitions()`, `fetch_athletes()`, `fetch_results()`, `fetch_live_events()`
— all returning **normalized domain payloads**, never raw provider JSON. Concrete
adapters (AthleticsProvider, OlympicProvider, …) are registered in a provider
registry keyed by data source; ingestion workers iterate registered providers.
Provider-specific response shapes stop at the adapter boundary.

## 4. Flutter layout (`app/lib/`)

```
core/
  config/       AppConfig (API base URL, devMockData flag) — env-driven
  network/      ApiClient (Dio), ApiException, interceptors
  routing/      GoRouter + StatefulShellRoute (bottom nav, state preserved)
  theme/        design tokens: AppColors, AppTypography, AppSpacing; dark+light
  widgets/      SportCard, AthleteCard, LiveBadge, SkeletonLoader,
                EmptyState, ErrorState, SectionHeader
features/
  home/         data-driven section feed (config-ordered sections)
  sports/       sports & disciplines catalogue
  live/         (shell now, Sprint 2 real) — honest "no live coverage yet" state
  games/        (shell now, Sprint 5 real)
  profile/      auth state, theme toggle, settings
  <feature>/    each: presentation/ (screens, widgets) + data/ (repository,
                models) + providers.dart (Riverpod)
```

State: Riverpod (`AsyncNotifier`/`FutureProvider`) — every remote read renders
loading (skeleton), error (retry), empty, and data states. Business logic lives in
repositories/notifiers, never in widgets.

## 5. Navigation (proposed UI map)

Bottom navigation (5 tabs): **Home · Live · Sports · Games · Profile**.
Push routes: `/sports/:id` (disciplines), `/athletes/:id`, `/competitions/:id`,
`/countries/:code`, `/search`. Deep-linkable via GoRouter paths.

## 6. Data honesty

- Mock/dev data only flows through `DevMock*Repository` implementations selected by
  `AppConfig.devMockData` (Flutter) / `settings.enable_seed` (backend). UI shows a
  "DEV DATA" chip whenever a mock repository backs a screen.
- Live indicators render only from a real `LiveEvent` with `status=live` from the
  backend; there is no client-side simulation path.
- AI content (Sprint 6) is always tagged `kind: prediction|estimate` end-to-end.

## 7. Cross-cutting

- **Security**: bcrypt password hashing, JWT access tokens, per-route auth deps,
  CORS allow-list from env, secrets only via environment.
- **Performance**: keyset-friendly pagination, indexed FK/filter columns,
  `selectinload` to avoid N+1, Redis caching layer (Sprint 2), image CDN-ready URLs.
- **Observability**: structured JSON logging hooks in `core/logging.py`; latency
  middleware; error tracking slot (Sentry-compatible) — wired in Sprint 7.
- **i18n-ready**: user-facing strings centralized; timestamps stored UTC, rendered
  in device timezone.
