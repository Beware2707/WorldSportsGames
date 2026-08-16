# Development Roadmap

## 0. Gate: tooling ✅ COMPLETE (2026-08-16)

- [x] Unreal Engine 5.8.1 (`C:\Program Files\Epic Games\UE_5.8`)
- [x] Visual Studio 2022 17.14 — game-dev C++ workload
- [x] Android SDK android-34 / NDK 27.2 / Temurin JDK 21 (see
      `TOOLCHAIN_SETUP.md` for the Java-25 Gradle trap)
- [x] Disk verified
- [x] Physical device: realme C73 5G (RMX3945), USB debugging authorized

Backend work never waited on this gate and is already done (§8 of
`BACKEND_MIGRATION.md`).

## 1. Phase 0 — Repository audit ✅ COMPLETE

This document set. Findings in `MIGRATION_ANALYSIS.md`.

## 2. Phase 1 — Foundations (parallel tracks)

**Track A — backend (unblocked, start now)**

1. `career_athlete` + attributes + migration
2. `game_result` + validation service (plausibility, attribute ceiling, splits)
3. Per-event leaderboards (validated results only)
4. Cloud save with additive conflict resolution
5. pytest coverage for each

**Track B — Unreal ✅ COMPLETE (2026-08-16)**

1. [x] Project + module skeleton per `UNREAL_ARCHITECTURE.md` (`game/`)
2. [x] Core subsystems (Online, Save, Progression, SportRegistry,
       DeviceProfile, Analytics, AudioDirector)
3. [x] HTTP client + auth against the live backend
       (`LiveBackend.AuthRoundTrip`); WS reuse comes with live leaderboards
4. [x] Sport/event definition data assets + value-kind scoring strategies
5. [x] Modular athlete pawn (leader-pose parts; AnimBP content is Phase 2)
6. [x] Camera director, HUD widget base
7. [x] Android packaging proven end-to-end (`WorldSports-arm64.apk`)

**Exit criterion met:** the empty-but-real app authenticates against the
backend (live automation test), installs on the physical device, launches,
and renders (engine touch interface over the entry map, landscape,
process stable). Adversarial review of the skeleton: 7 confirmed findings
fixed in `c8cde3b`, including server-side idempotent result submission.

## 3. Phase 2 — 100m vertical slice

The brief's §35 list, in dependency order. Nothing else starts until this is
finished and *feels good*.

| # | Deliverable |
|---|---|
| 1 | Sprint level: track, blocks, finish line, baked lighting |
| 2 | Athlete pawn + sprint locomotion (idle→drive→upright→fatigued) |
| 3 | Race state machine: Ready→Set→Go→Active→Finish→Result |
| 4 | Start mechanic: hold, gun, reaction capture, false-start rule |
| 5 | Sprint simulation: cadence accuracy → speed/fatigue |
| 6 | Input prototypes ×3, playtested; commit to one |
| 7 | AI opponents on the same simulation, 5 difficulties |
| 8 | Camera set: blocks, tracking, side, finish, celebration |
| 9 | HUD: countdown, reaction, speed, stamina, position, splits |
| 10 | Finish detection + timing to 0.001 s + photo finish |
| 11 | Result screen: position, time, splits, PB, XP, rewards |
| 12 | Submission with offline queue + server validation |
| 13 | Local save + cloud sync |
| 14 | Leaderboard view (global/country) |
| 15 | Main menu → Quick Play → race → result → menu |
| 16 | Pause, restart, settings, quality override |
| 17 | Audio: gun, crowd, footfalls, announcer, music |
| 18 | Replay of the finish |
| 19 | Android build on a real device at target frame rate |
| 20 | C++ automation tests for simulation, scoring, save |

**Definition of done:** a stranger installs the APK, plays a race, gets a time
they understand, sees it on a leaderboard, closes the app, reopens it and their
progress is there.

### Measured on device (realme C73 5G, RMX3945) — 2026-08-16

A live 8-runner race, Development build, ASTC, default quality tier:

| Metric | Value |
|---|---|
| Frame | 17.10 ms (~58 fps) |
| Game thread | 5.99 ms |
| Draw thread | 5.54 ms |
| RHI thread | 4.93 ms |
| GPU | 15.95 ms |
| Memory | 733 MB |

Comfortably past the 30 fps target for this tier, with the GPU as the
closest constraint — expected, since the scene is unlit-ish primitives and
the cost is mostly resolution. Re-measure once real athlete meshes,
animation and stadium content land; these numbers are the floor to defend,
not a result to bank.

### Known limits of the slice (require server authority, not client patches)

Two review findings cannot be closed on the client, because the client is
the thing that would have to be trusted. Both are cheap to close once the
server issues and verifies race conditions:

1. **The race seed is client-chosen and freely re-rolled.** Wind comes from
   that seed, and a legal +2.0 m/s tailwind is worth roughly 0.16 s over
   100 m, so a player can restart until the weather suits them. Fix: the
   server issues a signed race seed on request and re-derives the wind from
   it when validating, rejecting any submitted wind that does not match.
   The client already submits `rng_seed`, so only the server side is
   missing.
2. **`input_digest` is a breadcrumb, not proof.** The digest is computed and
   sent, but the input trace itself is not, so the server cannot recompute
   it. Combined with the attribute ceiling's deliberate tolerance, a
   modified client could submit a time near the ceiling. Fix: submit the
   trace for leaderboard-eligible runs and re-simulate server-side — the
   simulation is already deterministic and tested to prove exactly that is
   possible.

Neither is a reason to hold the slice: today's server validation still
rejects physically impossible results, false starts, incoherent splits and
over-ceiling times, and every rejection is stored for audit.

## 4. Phase 3 — Career & progression

Career athlete creation and customization, attributes and training mini-games,
stage progression, equipment, achievements, statistics, records.

## 5. Phase 4 — Tournament

Bracket state machine, qualification→heat→semi→final, medal ceremony,
broadcast presentation reserved for finals.

## 6. Phase 5 — Athletics expansion

200m/400m (data-only if the framework is right — this is the test of §5 of
`SPORT_SYSTEM.md`), 800m/1500m, hurdles, relays, jumps, throws, decathlon.

**Checkpoint:** if adding the 200m requires C++ changes, the sport framework is
wrong and must be fixed *before* the remaining ~50 sports are built on it.

## 7. Phases 6–11

Aquatics → remaining summer sports → winter sports → online competition →
monetization → optimization → release. Each new `EventKind` is a real
investment; each new event within an existing kind should be near-free.

## 8. Working rules

**Vertical slices only** (§34). Every sport reaches
input→gameplay→result→reward→progression→save before the next begins. A half-
finished sport is worth nothing; ten half-finished sports are worth less than one
complete one.

**Per-phase exit checklist:**
- Builds clean, no new warnings
- C++ automation tests green
- Backend pytest green
- Runs on a real mid-range Android device at the tier's target frame rate
- Memory within budget; profiled, not guessed
- Android packaging verified
- Documentation updated

**Never claim a feature works without running it** (§47). The platform work in
this repo repeatedly found that reviews and tests caught defects that reading the
code did not — the same discipline applies to gameplay, where "feels right" is
also a testable claim on a real device.

## 9. Risk register

| Risk | Mitigation |
|---|---|
| Input model doesn't feel good | Prototype 3, playtest on device before committing (Phase 2 #6) |
| Sport framework leaks sport-specific code | 200m must be data-only; checkpoint at Phase 5 |
| Mobile performance misses target | Budgets + on-device profiling from the first playable build |
| Cheating on leaderboards | Server validation from day one, not retrofitted |
| Scope explosion across 60 sports | Strict vertical slices; ship the 100m before anything else |
| Asset pipeline / IP contamination | Original or properly licensed assets only; never reference-game assets |
