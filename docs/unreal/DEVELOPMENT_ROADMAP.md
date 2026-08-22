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
| The client scored marks the server refuses | Taking off ~4.5m behind the board still reaches the sand, so the client measured ~0.7m against the server's 1.00m minimum — a jump shown, counted, then refused |

**Verified on the emulator, end to end:** a full series of three attempts
reading `1. 3.17 m · 2. X over the board · 3. X over the board · Best
3.17 m`, with the result headline showing the mark rather than a position
and a time. Both foul kinds, the no-mark result, and a legal mark measured
from the board have all been seen on device.

**A playtest question for a human, not a defect:** the athlete reaches the
board near 9 m/s and a jump carries about five metres, so the window that
scores is under half a second. A green NOW cue in the last 2.2m makes it
hittable — a real jumper knows when they are on the board rather than
computing metres — but whether it is *fair* needs someone playing it.

### 6.6 Throws (shot put) — a release, not an approach

A fourth event kind. There is no approach to run and no takeoff to place:
the athlete winds up in place, power rises to a peak and then falls away
because a throw held too long is one they have already begun to unwind
from, and letting go is a single decision that sets both the speed the shot
leaves at and — through technique — its angle. Holding on carries the throw
out of the circle: a foul and no mark, the same rule as a jumper past the
board.

The field-event path is now shared with the jumps: one attempt series, one
scoreboard, one submission. What differs stays honest — a jump reports the
wind that stood for its best attempt, a throw reports none, because the
sport records no wind for throws.

**The anti-pay-to-win claim is narrower than first assumed.** Measured over
24 throws, wild timing still returns about three quarters of an athlete's
ceiling, so execution closes an attribute gap of roughly 15-20 points and
no further: a judged thrower at 55 beats a wild one at 70 and loses to a
wild one at 85. The test states that boundary rather than asserting a
guarantee the model does not provide. What training buys is a ceiling, not
a result — and that is worth remembering when tuning any later event.

Three defects came from running against the real server rather than from
any offline test:

| Defect | Why it hid |
|---|---|
| Live tests created athletes with gender "female" against a `^[MFX]$` schema | The smoke athlete already existed, so the create path only ran for a NEW account. The game's own screen was always correct |
| The live suite outgrew the server's 10-results-per-minute anti-script cap | Thirteen submissions landed on one athlete inside a minute; the fix is spreading the load, not raising the cap |
| The bracket test assumed a tournament starts at qualification | A tournament survives the app closing, so the test can meet one already under way |

### 6.7 High jump — the first event with no fixed number of attempts

The fifth kind, and the one that broke an assumption baked into the field
path: that a competition is a fixed series. A high jump is a **ladder**.
The bar starts at an opening height and rises by a fixed increment every
time it is cleared; the day ends on the third failure at one height,
whenever that comes. The recorded mark is the highest bar CLEARED — not
how high the arc actually went, because clearing 2.05 m by a foot still
records 2.05 m.

That single difference reached into every layer, and each place it was
missed said something untrue:

| Layer | What a fixed series would have claimed |
|---|---|
| Game mode | "attempt 4 of 3" — a jumper on their second try at 1.85 m is not on attempt 4 |
| Scoreboard | a list of distances, when a high jump card is heights and O/X |
| HUD | "PAST THE BOARD", when overrunning the mark is a bad jump, not a foul |
| World | a sand pit and no bar — the player aiming at nothing they can see |
| Result | "No mark", when the sport says a height was never cleared |
| Live submit | a wind reading, which World Athletics records for the horizontal jumps only |

**Calibration found a real trap.** Every other event uses a power curve
with an exponent below 1 to model diminishing returns. Against a server
ceiling that is a straight line between two endpoints, a concave curve
sits *above* that line in the middle — so a mid-attribute athlete could
clear a height the server would then refuse. The vertical branch therefore
carries its own `HeightCurve`, fixed at 1.0, and endpoints deliberately
under the server's (0.97/2.38 against 1.00/2.42). Perfect play now tops
out at 1.25 m / 1.70 m / 2.10 m for attribute levels 25 / 55 / 85, against
ceilings of 1.35 / 1.78 / 2.21 — approaching them from below at every
level, which is the invariant the whole game rests on.

### 6.8 Pole vault — the row the ladder paid for

A whole event for thirty lines of data. The high jump's ladder, its bar,
its card, its "no height" and its live submission all took the pole vault
without a line of new code: a longer runway, a bar that starts at 2.60 m
and moves in tens rather than fives, and `max_speed` in the governing set
because a vault is bought with speed carried onto the pole where a high
jump leans on the plant. Perfect play tops out at 3.00 / 4.20 / 5.40 m for
attribute levels 25 / 55 / 85, against ceilings of 3.14 / 4.38 / 5.63.

That is what the framework claim is supposed to buy, and it is the second
time it has paid out — the discus and javelin were rows too.

### 6.9 Triple jump — the first event with more than one takeoff

The sixth kind. Everything before it turned on ONE decision: a gun, a
board, a release. A triple jump has three takeoffs, and the two between
them are the event — the approach buys the speed, but the rhythm decides
how much of it survives to the sand.

- The board takeoff is judged as a long jump's is, by the gap to the board.
- Each phase then flies for as long as the ground it covers takes at the
  speed it is covered at, and a window opens around its landing.
- A takeoff inside that window carries the jump on; the further from the
  landing, the more of the NEXT phase is lost — a bad step is short, not a
  bad hop retroactively.
- Missing the window entirely is a stumble, not a foul: the jumper carries
  on, just shorter. Measured at attribute level 55, timing both
  transitions is worth about a metre — 11.76 m against 10.77 m.

**Spamming the button is the worst thing a player can do.** The window
opens before the landing, so a tap the instant it opens is as far from the
landing as a tap can be. That falls out of the model rather than being
enforced, which is the right way for it to be true.

Two calibration facts came out of it:

| Finding | Why |
|---|---|
| The long jump's speed exponent (0.68) bulges a triple jump ABOVE its ceiling through the middle of the range | Distance goes as the square of speed while the ceiling is linear; a triple jump spans nearly twelve metres between endpoints where a long jump spans five, so the same exponent misfits by more. 0.85 straightens it |
| The jumps' "approach the ceiling" tolerance was a fixed 0.75 m | Vacuous for a long jump (8.5% of its ceiling), impossible for a triple jump (4.1%). Eight per cent says the same thing about both — the same fix the javelin already forced on the throws |

### 6.10 What the emulator found this round

Seven more, and every one of them the screen or the world saying something
the sport does not:

| Defect | What it claimed |
|---|---|
| "Stamina 100%" on a high jump | A field event has no stamina model, so the number could never have moved |
| "No mark" when nothing was cleared | The sport's word is NO HEIGHT. A bar never cleared is not a mark never measured |
| The crossbar drawn at its regulation 30mm | One pixel from the far end of a 40m runway — the player aiming at something invisible. Furniture the player must AIM at is now drawn oversized on purpose, and coloured against the track rather than white, which the lane lines already are |
| "No height — 2.60 m was never cleared" under a headline reading "No height" | The detail line said nothing the headline had not |
| "Race again" on a jump | A jumper does not race again |
| A javelin thrown from a CIRCLE, fouled by being "carried out of" it | The shot and the discus are thrown from a circle. A javelin is thrown from a runway over a foul arc, and its foul is crossing that arc |
| No jumper ever left the ground | Every jump resolved at the instant of takeoff, so the world showed an athlete sliding fifteen metres along the runway |

The last one is the interesting one. The fix was to give **every** jump a
flight: the mark is still decided at takeoff, but the simulation now runs
the athlete through the air before it measures — one phase for a long
jump, three for a triple jump, and an arc that rises to the height a
vertical jumper actually reached, so a clearance looks like one. Fifty-two
tests measured the same marks to the centimetre afterwards, which is what
made it safe to do.

**Instrumenting beat guessing again.** A `WSField` line on the in-game
status command — event, attempt, bar, failures, metres to the board,
phase, window — turned "did that clear?" into a reading. It is how the
pole vault's ladder was verified at 2.60 m: `best=2.60 bar=2.70 fails=0`
in one line.

### 6.11 Relays — the first event decided between athletes

The seventh kind, and the last of the athletics ones. Everything before it
turned on one athlete's decisions. A relay turns on what happens BETWEEN
four of them.

- Each leg is a sprint, judged on the same cadence. The first starts from
  blocks off a gun; the other three start at whatever speed the exchange
  preserved, which is why a relay team is faster than four individual runs
  added together.
- The baton must change hands inside the takeover zone — 30 m for the
  4x100, 20 m for the 4x400 — and the ideal point is near the end of it,
  with the outgoing runner already at speed.
- **Outside the zone is a disqualification, not a slow time.** A team that
  misses it gets no time at all, modelled exactly as a false start is. The
  same rule covers running through the zone still holding the baton.
- Fatigue resets at every exchange, because the next leg is a different
  athlete. The ceiling does not: a team is four runners of the player's
  quality, so one athlete's attributes still bound the whole clock.

Measured at attribute level 55, handing over on the mark is worth 1.29 s
over handing over as the zone opens in the 4x100, and 0.85 s in the 4x400.

**The PASS button only exists inside the zone.** Pressing it outside would
disqualify the team, so the HUD does not offer it there — the one place in
the game where a control is withheld rather than merely unhelpful.

Two calibration notes:

| Finding | Why |
|---|---|
| The two relays needed opposite corrections — the 4x100 too FAST at low attributes, the 4x400 far too SLOW everywhere | The 4x400's fatigue numbers were inherited from the individual 400m, where one athlete runs the whole thing. A relay leg starts on fresh legs AND at speed, so the same fatigue depth ate half the team's speed |
| Both needed a speed exponent above 1.0 (1.70 and 1.35) | The same reason the sprints do, only more so: the ceiling is linear in the attribute mean while time goes as 1/V, and a relay's ceiling spans 27 s (4x100) and 119 s (4x400) between its endpoints |

**The track was 1500 m and the 4x400 is 1600 m.** The test that reads every
event table and fails if one outruns the world caught it before the
emulator did — the third time that constant has been too short, and the
first time it was caught by a test rather than by a runner disappearing
into black nothing.

### 6.12 What the emulator found on the relays

Three more, in one race:

| Defect | What it claimed |
|---|---|
| A team disqualified for running through the takeover zone was told "False start — no time to submit" | They had started cleanly. Two disqualifications with two different causes were sharing one message, and the one shown named the wrong rule — and the wrong fix |
| "0th --.-- (+0.0 m/s)" on the result | A relay records no wind. A reading of zero is not "no reading" |
| The takeover zone was a thirty-metre slab of gold across all eight lanes | It filled the screen and read as sand. A real track marks the zone with two LINES across it, which is what it draws now |
| "Splits 200m 18.12 300m 18.39 400m 21.40" on a relay | A relay's splits are LEGS. Labelling them by distance called the first handover "200m" and dropped the opening leg entirely — a description of a race nobody ran |

The first one is the interesting one, because the model was already right:
`bBadExchange` was set, carried into the outcome, and then thrown away by a
verdict that only knew the word "false start". The fix was to let the
message ask which rule was broken.

**A note on the emulator itself.** The game keeps running between adb
commands, so anything that pauses to look at a screenshot loses the race.
A full relay has to be played by ONE script that holds the blocks, taps
the cadence, polls the distance and calls the pass — returning to think
between steps put the athlete 60 m further down the track than the last
reading said.

### 6.13 The batched adversarial review

Run on 2026-08-22 over everything with review debt — Phase 3 backend, the
career client, the tournament subsystem and the whole athletics expansion:
20 commits and 12,865 changed lines. Eight findings, all fixed the same
day, each with a regression test.

**Three were in code written that same session**, which is the honest
price of "verified on the emulator and against the server": the emulator
checks what the screen says, and one live submission checks one set of
numbers.

| Finding | Why the tests missed it |
|---|---|
| Relay leg splits included the reaction, so the server refused the result | The one live submission ever made had a 175 ms reaction and a slow team; the check's tolerance is 1% of the time, so it passed. A 1500 ms auto-release — which the emulator itself produced on its first attempt — fails |
| A duplicate PASS press disqualified the team | `bPassRequested` is cleared on a successful handover, so a second press in the same frame ran against the NEXT leg's zone |
| The triple jump's miss penalty was diluted and leaked into later phases | Subtracting it from the whole remaining total charged 5.8% instead of 9%, a third of it to a phase that was timed perfectly |
| High jump and pole vault claimed a wind reading | The live TEST was corrected to send none; the game's own submit path was not, so the two disagreed about the contract |
| A post-round tournament refresh could be silently dropped | `SubmitRound` does not set `bRequestInFlight`, so `Refresh()` could return early and leave a stale bracket |
| An unparseable 200 cleared the bracket and reported success | `FJsonSerializer::Deserialize`'s return value was ignored |
| Training's retry path could raise on a NULL `client_ref` | The first lookup is guarded by `is not None`; the rollback lookup was not |
| Relay passes used the simulation's clock while taps used the race clock | The trace is hashed into `InputDigest` and submitted for replay — and a two-clock trace cannot be replayed |

**Isolating a model needs a closed loop, not a deleted input.** The first
attempt at a regression test for the triple jump removed one takeoff from
a fixed trace. That does not isolate anything: a shortened phase lands
earlier, so the NEXT takeoff — still stamped at its original time — is
late as well, and the two effects are indistinguishable. The test now
generates its traces against a live shadow simulation, tapping each
transition on its own landing except the one being missed. Measured that
way: missing the step costs 0.95 m and missing the jump 1.08 m, in
proportion to their shares, each charged to its own phase.

### 6.14 Deliberately still open

- **The 200m and 400m are run on a straight track.** Bends need track
  geometry, lane-stagger and a camera that follows a curve; the simulation is
  distance-correct but the presentation is not yet event-correct.
- **`recovery` counts toward the 400m ceiling but has no distinct simulated
  effect** beyond the mean. It should govern how fatigue *clears*, which needs
  a between-rounds model that does not exist yet.
- **A relay is eight capsules, one per team.** One actor stands in for four
  runners: the simulation runs all four legs, but the world shows a single
  athlete covering the whole distance. Four visible runners per lane and a
  baton that visibly changes hands is presentation work, not simulation
  work, and it has not been done.
- **The triple jump's transition windows are 0.18s either side of each
  landing, and no human has tried them yet.** They are most of a 0.4s
  flight, and the phases are near enough uniform that the rhythm can be
  learned — but that is a claim, not a measurement. It belongs with the
  long jump's takeoff window on the list of things a person has to play.
- **A high jumper cannot pass a height.** The real sport lets an athlete skip
  a bar to save legs for a higher one, and lets them enter above the opening
  height. Both are tactics, and both need a UI decision point the ladder does
  not have yet.

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
