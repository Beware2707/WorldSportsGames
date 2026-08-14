# Migration Analysis — Flutter platform → Unreal Engine 3D game

Audit date: 2026-08-10 · Audited commit: `da407e6`

## 0. Two corrections to the brief before anything else

**There is no `WorldSportsGames.zip`.** No such archive exists on this machine
(`~/Downloads` contains 19 zips; none is it). The project it describes is
present as a **live working tree** at `C:\Users\riddl\Downloads\WorldSportsGames`
with full git history — the platform built over seven sprints in this same
session. That is strictly better than an archive: the audit below is against
real code with commit history, not a snapshot. No zip was extracted because
none exists.

**Unreal Engine is not installed, and neither is a C++ toolchain.** Checked:
`C:\Program Files\Epic Games`, `%LOCALAPPDATA%\UnrealEngine`, `D:\Epic Games`
— no engine. No Visual Studio, no MSVC on `PATH`. This blocks every phase past
this document: an Unreal project cannot be created, compiled, or packaged for
Android without them. The analysis is unaffected and is delivered in full; the
implementation phases are gated on installing the tooling (see §9).

Also flagged: the brief specifies **Unreal Engine 5.8**. Confirm that version is
actually available in the Epic Launcher before committing to it — pin whatever
the current stable release is rather than a version number that may not ship.

---

## 1. Current architecture

A production multi-sport **data platform** — not a game. Two deployables:

| Layer | Stack | Size |
|---|---|---|
| Backend | FastAPI, SQLAlchemy 2 (async), PostgreSQL, Redis, Alembic | 82 Python modules, 48 endpoints, 28 ORM models, 7 migrations |
| Client | Flutter 3.44, Riverpod 3, GoRouter, Dio | 56 Dart files, 22 screens |
| Tests | pytest + flutter_test | 143 backend, 74 Flutter (all green) |

Layering is strict and one-directional: `api → services → repositories → models`.
Nothing imports upward. This matters enormously for the migration — the HTTP
layer is a thin shell over reusable logic.

```
backend/app/
  api/v1/    routers only, no business logic   ← Unreal will call these
  services/  use-case orchestration            ← reusable as-is
  repositories/ data access, async, no N+1     ← reusable as-is
  models/    28 typed ORM models               ← reusable as-is
  ai/        provider-agnostic insight engine  ← reusable, low game priority
  core/      config, security, cache, logging, middleware
```

## 2. Existing functionality

**Accounts & identity** — bcrypt + JWT, register/login/me, `Settings` refuses to
boot with the dev secret outside debug.

**Sport taxonomy** — 54 sports across summer/winter/LA28 with disciplines,
seeded as reference data. Adding a sport is a seed row.

**Competitive data** — competitions, editions, events, participations,
normalized results (`value_kind` + canonical text + comparable magnitude),
rankings (explicit `methodology`, versioned by `as_of`), records, medals.

**Live** — `LiveEvent`/`LiveUpdate`, WebSocket diff streaming with Redis
pub/sub fan-out, dev-gated simulator.

**Personalization** — polymorphic follows, server-composed home feed,
notification preferences (opt-out model).

**Games** — 8 mini-games over 4 mechanics, XP/levels, streaks, achievements,
leaderboards. **This is the single most reusable subsystem for the 3D game.**

**Media & notifications** — link-out media model, notification fan-out with
`dedupe_key` idempotency.

## 3. What is directly reusable (keep, do not rewrite)

| Subsystem | Why it transfers |
|---|---|
| **Auth** (`core/security.py`, `api/v1/auth.py`) | JWT is transport-agnostic. Unreal sends the same `Authorization: Bearer` header Flutter does. Zero change. |
| **Games/progression** (`services/games.py`) | Already models exactly what the game needs: server-authoritative XP, per-game `score_direction`, personal bests derived from an append-only session log, direction-aware achievements, one-row-per-player leaderboards. The 100m sprint is a `lower_better` timed game — the existing shape fits without modification. |
| **Leaderboards** (`repositories/games.py`) | Already global/country/friends-scoped and one row per player. |
| **Athlete/country/sport catalogue** | The game needs nations, flags, disciplines. Already seeded and served. |
| **Records** (`models/competitive.py`) | PB/SB/NR/WR already modelled with units and comparable magnitudes — the brief's §43 record system exists. |
| **Rankings** | Career-mode tiering can read the existing ladder model. |
| **Observability & rate limiting** | Structured logging with credential redaction, request ids, credential-endpoint limiting. Applies unchanged. |
| **Migrations, seeds, CI, Docker** | Infrastructure carries over untouched. |

**Roughly 80% of the backend is reusable as-is.** The platform was built with
"never trust the client for persistent values" already enforced, which is
exactly the brief's §23 requirement.

## 4. What must be rebuilt (cannot transfer)

Everything presentational. The Flutter app is a **data-browsing client**; a 3D
game shares none of its rendering:

- All 22 Flutter screens
- The Riverpod state graph, GoRouter navigation, Dio client
- The design system, widgets, theming
- The four Flutter mini-game engines (reaction/accuracy/timing/sequence) —
  these are 2D touch prototypes. **Their scoring semantics survive; their
  implementation does not.** The reaction engine's false-start detection and
  randomized hold are directly instructive for the 100m start, but must be
  rewritten as Unreal gameplay.

## 5. What is missing entirely (new build in Unreal)

Nothing 3D exists. All of: characters, animation, physics, cameras, stadium
environments, race simulation, AI opponents, HUD, audio, VFX, replay, celebration,
career mode, tournament brackets, training mini-games, equipment/customization,
offline play with cloud-save conflict resolution, analytics, and Android
packaging.

## 6. Problems found in the existing project

Carried forward honestly rather than hidden:

1. **No real data ingestion.** Every live/result row comes from the dev
   simulator or fixtures. The `SportsDataProvider` adapter seam exists but no
   provider is implemented. *For the game this barely matters* — a game
   generates its own results. It matters if the companion app ships.
2. **No push transport.** Only the in-app inbox; `NotificationTransport` is an
   unimplemented protocol.
3. **No social graph.** The friends leaderboard is honestly empty. The game
   needs one for §20.
4. **Notification generation has no production caller** — the pipeline exists
   and is tested but nothing invokes it.
5. **In-process rate limiter** — with N replicas the effective limit is N×.
6. Known-open lower-severity items are listed in `PROJECT_PLAN.md`.

## 7. Recommended migration strategy

**Additive, not destructive.** The backend is a service the game consumes; the
Flutter app becomes an optional companion.

```
            ┌──────────────────────────┐
            │  Unreal 5.x mobile game  │  ← NEW: all gameplay/3D
            └────────────┬─────────────┘
                         │ HTTPS + WS (same /api/v1)
            ┌────────────▼─────────────┐
            │  FastAPI backend (KEEP)  │  ← ~80% unchanged, extended
            │  PostgreSQL · Redis      │
            └────────────▲─────────────┘
                         │ same API
            ┌────────────┴─────────────┐
            │ Flutter app (OPTIONAL)   │  ← results/news companion
            └──────────────────────────┘
```

**Do not delete the Flutter app.** It costs nothing to keep, it is the live
proof the API works, and its 74 tests are a regression net over the shared
contract. Decide its fate after the 100m slice ships — that decision needs no
commitment now.

### What moves where

| Concern | Home |
|---|---|
| Rendering, physics, animation, camera, input, audio, VFX | **Unreal** |
| Race simulation, local result computation, presentation | **Unreal** |
| Accounts, XP, progression, inventory, achievements, leaderboards, records, cloud save, validation | **Backend** |
| Sport/discipline/nation catalogue | **Backend** (already there) |
| Result *validation* (anti-cheat) | **Backend** (new work — §BACKEND_MIGRATION) |
| Browsing real-world sports data, news | **Flutter companion** (or retire) |

## 8. Backend work the game requires (new)

1. **Athlete/career models** — the current `Athlete` is a *real-world* athlete
   (a data record). The game needs a **player-owned career athlete**:
   appearance, attributes, equipment, career stage. This is a new aggregate,
   not a modification of the existing one. Conflating them would corrupt the
   catalogue.
2. **Result validation** — the current games API accepts a score with only
   per-engine bounds. A competitive 100m needs plausibility checks
   (reaction ≥ ~100 ms, time vs attributes, split coherence) plus replay-seed
   submission.
3. **Tournament/bracket progression.**
4. **Cloud save with conflict resolution.**
5. **Social graph** for friends leaderboards.
6. **Analytics event ingestion.**

## 9. Blockers (must be resolved before implementation)

| Blocker | Impact | Resolution |
|---|---|---|
| Unreal Engine not installed | Cannot create/build/package the project | Install via Epic Launcher; confirm the actual current version |
| No Visual Studio / MSVC | Cannot compile C++ | Install VS 2022 with "Game development with C++" + Android workload |
| Android SDK/NDK/JDK unverified | Cannot package APK/AAB | Install via Unreal's `SetupAndroid`, or Android Studio |
| ~150–200 GB disk | Engine + project + derived data | Verify before install |

Until these land, work is limited to documentation, backend extension, and
asset-pipeline preparation — **all of which are genuinely useful and none of
which require the engine.**

## 10. Answers to the brief's A–H

**A. Reuse** — auth, games/progression/XP/achievements/leaderboards, sport &
nation catalogue, records, rankings, observability, rate limiting, migrations,
seeds, CI, Docker. ~80% of the backend.

**B. Rebuild** — the entire Flutter presentation layer and all four mini-game
engines (semantics survive, code does not).

**C. Move to Unreal** — all rendering, gameplay, physics, animation, camera,
input, audio, HUD, local simulation and presentation.

**D. Stays backend** — accounts, persistence, progression, competitive
validation, leaderboards, cloud save, catalogue, notifications, analytics.

**E. Remove** — nothing yet. Retiring the Flutter app is a post-slice decision;
deleting it now destroys a working reference client and its test suite for no
gain.

**F. Redesign** — the athlete model (split real-world vs. career athlete);
score submission (add validation + replay seed); leaderboards (add per-event
scoping); notifications (add a real transport).

**G. Unreal project structure** — `UNREAL_ARCHITECTURE.md`.

**H. 100m vertical-slice plan** — `DEVELOPMENT_ROADMAP.md` §3.
