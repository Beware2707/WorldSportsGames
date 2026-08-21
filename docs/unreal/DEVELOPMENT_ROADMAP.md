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

### Status (2026-08-16)

| # | Deliverable | State |
|---|---|---|
| 1 | Sprint level | ✅ procedural track, lines, blocks, 10m marks, backdrop |
| 2 | Athlete pawn + locomotion | ⚠️ placeholder capsules with a stride bob; needs character art and an AnimBP |
| 3 | Race state machine | ✅ on the sport-agnostic phase machine |
| 4 | Start mechanic | ✅ hold/gun/reaction, rule-accurate false start, randomised set pause |
| 5 | Sprint simulation | ✅ deterministic, calibrated to the server ceiling |
| 6 | Input prototypes ×3, playtested | ❌ only rhythm-and-hold exists; the other two and the playtest need a human |
| 7 | AI opponents, 5 difficulties | ✅ same simulation, input-quality tiers, ordering asserted |
| 8 | Camera set | ✅ blocks / chase / finish |
| 9 | HUD | ✅ clock, reaction, speed, cadence band, stamina, splits |
| 10 | Finish detection + timing | ✅ interpolated 10m marks, ms truncation |
| 11 | Result screen | ✅ position, time, wind, full field, server verdict |
| 12 | Submission + offline queue | ✅ per-account queue, idempotent replays |
| 13 | Local save + cloud sync | ⚠️ subsystem done and tested; the race does not write career save yet |
| 14 | Leaderboard view | ✅ global board, verified fetching real rows on device |
| 15 | Menu → race → result → menu | ✅ |
| 16 | Pause, restart, settings, quality | ✅ pause cannot buy time or disqualify |
| 17 | Audio | ⚠️ procedural gun/calls/footfalls/finish; needs authored sound, and audibility is unverified by ear |
| 18 | Replay of the finish | ✅ presentation only, result provably unchanged |
| 19 | Android build at target frame rate | ✅ ~58 fps on a realme C73 5G |
| 20 | C++ automation tests | ✅ 27 green |

The gaps are honest ones: character art and animation, the two unbuilt input
prototypes and the playtest that picks between them, authored audio, and
wiring the race into cloud save. None of them is blocked by the framework.

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

### 6.1 Checkpoint result: FAILED, then fixed

The framework **failed** the checkpoint. Adding the 200m was not a data change,
because the running event was not data:

| What was wrong | Where |
|---|---|
| Race distance was a compile-time constant | `FWSSprintSimulation::RaceDistance` |
| Splits hardcoded to ten 10m marks | `WSSprintSimulation.cpp` |
| Cadence curve keyed on absolute metres | `TargetCadenceAt` |
| Event code was a literal at the submit site | `WSSprintGameMode.cpp` |
| Race distance duplicated in the rival replay | `WSSprintRunner.cpp` |
| Leaderboard URL hardcoded `sprint-100m` | `RefreshLeaderboard` |

Fixed by making the event a row in a table (`WSSprintEvents.h/.cpp`,
mirroring the backend's `EVENTS`). The cadence profile is now expressed in
**fractions of the race** rather than metres, so one curve describes a 100m
and a 400m. Adding the 400m after that was genuinely a row plus calibration
constants — no new code path, no new branch.

**Status: 200m and 400m playable, calibrated, and accepted by the live
backend.** 29 offline automation tests and 4 live-backend tests green.

### 6.2 What the calibration work found

Two defects that only a per-event ceiling sweep could surface:

1. **`recovery` never reached the simulation.** The backend counts it among
   the 400m's governing attributes; `FWSSprintAttributes` had no such field
   and `ByKey` silently returned 40. The client and server therefore computed
   *different* means for the same athlete, so the ceiling shown to the player
   was not the one the race ran against. `ByKey` now `ensure`s on an unknown
   key rather than inventing a number.
2. **A lopsided athlete could beat the server's ceiling.** The ceiling is a
   function of the governing *mean*, but the simulation let a single attribute
   (stamina, via the fatigue term) buy speed beyond it — an honest run the
   validator then rejects. Every per-attribute effect is now capped at the
   governing mean, which makes "no attribute beats the ceiling" structural
   rather than a constant to be re-tuned. Training is still rewarded: every
   governing attribute raises the mean itself.

The calibration tests sweep **32 seeds per attribute level** and play at the
fastest legal reaction (101ms), because wind is seeded and worth more than a
second over 400m — a three-seed sample passed while the strongest legal
tailwind sailed under the ceiling.

### 6.3 Middle distance (800m / 1500m) — a new event KIND

Not a longer sprint, and treating it as one would have produced a bad game.
Three things make it a different kind:

- **No blocks**, so no reaction to measure (`requires_reaction=False`).
- **No wind.** World Athletics records none beyond 200m, so reporting one
  would invent a measurement the sport does not take.
- **The skill is pace judgement against a finite energy budget**, not stride
  cadence. Tapping a rhythm for four minutes is not a game.

Lives in `WSPaceSimulation.h/.cpp` as `FWSMiddleDistanceSimulation`. What
carries over from the sprint is exactly the part that makes results
trustworthy: fixed-step deterministic integration, a seeded race, the
server's ceiling model, and the per-attribute cap at the governing mean.

**The live backend caught a defect no offline test could:**

```
middle-800m: 130.373s was rejected — beyond this athlete's ceiling (130.400)
```

The calibration had treated *the fastest pace you can hold without emptying
the tank* as perfect play. It is not: going a shade over and dying over the
last 100m is faster, exactly as on a real track. The reference therefore
understated the true optimum, and an honestly run race crossed the server's
limit. Fixed by (a) searching for the effort that minimises finish time
directly — a coarse scan plus local refine, **not** a ternary search, since
time against effort is not unimodal and ternary converged into a basin 19s
slower — and (b) a test that sweeps a *family* of 75 pacing strategies per
point and asserts none beats the ceiling. A player will always find a line
the designer did not model, so the guarantee has to hold for lines nobody
modelled.

**Status: simulated, calibrated (margins +1.9s to +6.5s), and accepted by
the live backend. NOT yet playable** — there is no game mode or HUD for the
pace input model, so the vertical slice is not closed.

### 6.4 Hurdles (110m / 400m) — a skill layered on the sprint kind

NOT a new kind, and the distinction matters: a hurdles race keeps the
blocks, the reaction, the wind and the cadence band. The barriers are three
numbers in the event row (count, first barrier, spacing), and the takeoff is
judged in **metres** rather than seconds — the barrier stands at a place on
the track, so a slower athlete legitimately leaves the ground later on the
race clock.

Calibration found that **barriers alone cannot make a hurdles race**. With
only a per-barrier speed penalty the athlete re-accelerated between them and
the whole field finished under the server's ceiling — the 110m hurdles ran
11.05 against a 12.90 limit. Two real things were missing:

1. **A hurdler cannot run at flat-sprint speed.** The stride pattern between
   barriers is fixed at three strides, which caps the top end however fast
   the athlete is on the flat.
2. **Technique must govern the cost of a *clean* clearance**, not only the
   width of the timing window. Clearing low and landing running is
   technique: a novice loses ~4% per barrier where a specialist loses ~0.4%.
   One factor for everyone left no room between a beginner and a champion.

The player gets one contextual action — with a barrier within 7m it is the
takeoff, otherwise the dip at the line. A test asserts the barriers are not
scenery: running into all ten costs ~1s against clearing every one.

### 6.5 Jumps — the first event measured in metres

`jump-long` is on the server. It is the first event where **higher is
better**, and nothing about that needed special-casing: the ceiling check,
the leaderboard aggregate, the personal-best comparison and the tournament
ranking all already branched on `lower_is_better` — those branches simply
had never been executed by any event. They are now covered by tests.

One real defect surfaced: `format_value` fell through to `%g` for a
distance, printing a jump of 8.90 m as "8.9". Athletics reports marks to the
centimetre and keeps the trailing zero; the difference is two marks a
centimetre apart reading as the same number.

**Built and playable.** A third event kind, earned by being three things in
sequence: an approach that is a sprint (and reuses the cadence skill), a
takeoff judged against a board, and a flight the player no longer controls.
A field event does not go through the race path at all — there is no
eight-lane field to keep in step with and no gun to react to, so the game
mode owns the simulation and drives the athlete directly.

The board is the event. The mark is measured FROM THE BOARD, so every
centimetre short of it comes off the jump while a single centimetre past it
is no mark at all. Three attempts; only the best legal one is submitted;
three fouls submit nothing, because a zero would be recorded as a jump.

Calibration and device testing between them found five defects:

| Defect | Why it mattered |
|---|---|
| A flawless approach fouled every time | Aiming at exactly the board overshoots it within one step; real jumpers aim short and use check marks |
| Mid-range could not reach its ceiling | Distance goes as the SQUARE of speed while the ceiling is linear, so the speed curve needs an exponent BELOW 1.0 — the mirror of the sprints' sag |
| A jump short of the pit scored "0.00 m" | There is no zero-metre jump; landing short of the sand is a failed attempt, and the server would refuse the mark anyway |
| Scoring a jump crashed the editor | The field-event guard was on GetState() alone, so the standings dereferenced a simulation that never existed |
| The HUD spoke race language | "0th --.-- (+0.0 m/s)", an "RT 0 ms" with no blocks, a board readout drawn over the main menu, and a "Replay finish" for an event with no finish |

### 6.6 Deliberately still open

- **The 200m and 400m are run on a straight track.** Bends need track
  geometry, lane-stagger and a camera that follows a curve; the simulation is
  distance-correct but the presentation is not yet event-correct.
- **`recovery` counts toward the 400m ceiling but has no distinct simulated
  effect** beyond the mean. It should govern how fatigue *clears*, which needs
  a between-rounds model that does not exist yet.

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
