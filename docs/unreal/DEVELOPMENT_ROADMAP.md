# Development Roadmap

## 0. Gate: tooling (blocking, not started)

Nothing in Unreal can begin until these exist on the machine. Verified absent
on 2026-08-10:

- [ ] Unreal Engine (confirm current stable version in the Epic Launcher)
- [ ] Visual Studio 2022 — "Game development with C++" + Android workload
- [ ] Android SDK / NDK / JDK (`SetupAndroid` or Android Studio)
- [ ] ~150–200 GB free disk
- [ ] A physical mid-range Android device for testing

**Work available in parallel that needs none of the above:** every step in
`BACKEND_MIGRATION.md` §8, plus asset-pipeline conventions. That is where effort
should go while tooling installs.

## 1. Phase 0 — Repository audit ✅ COMPLETE

This document set. Findings in `MIGRATION_ANALYSIS.md`.

## 2. Phase 1 — Foundations (parallel tracks)

**Track A — backend (unblocked, start now)**

1. `career_athlete` + attributes + migration
2. `game_result` + validation service (plausibility, attribute ceiling, splits)
3. Per-event leaderboards (validated results only)
4. Cloud save with additive conflict resolution
5. pytest coverage for each

**Track B — Unreal (blocked on the gate)**

1. Project + module skeleton per `UNREAL_ARCHITECTURE.md`
2. Core subsystems (Online, Save, Progression, SportRegistry, DeviceProfile)
3. HTTP/WS client + auth against the live backend
4. Sport definition data assets + `EventKind` scoring strategies
5. Modular athlete + shared skeleton + base AnimBP
6. Camera director, HUD framework
7. Android packaging proven end-to-end with a placeholder scene

**Exit criterion:** an empty-but-real app authenticates against the backend and
installs on a phone. Prove the pipeline before building content on top of it.

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
