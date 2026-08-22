# 100m Implementation Plan — M1 to M18

Written 2026-08-22, against the audit in
[CURRENT_IMPLEMENTATION.md](CURRENT_IMPLEMENTATION.md).

**The honest position.** Thirteen of these eighteen milestones are already
done and were done before this plan was written. Writing them up as future
work would be fiction. Each is listed with what closed it, how it was
tested, and what — if anything — is left inside it. The five that are
genuinely open are M10, M11, M12, M17 and M18, and every one of them is
**content**, not code.

Milestones are ordered as the brief specifies. Status is one of
**DONE**, **PARTIAL** or **OPEN**.

---

## M1 — Project and build verification · **DONE**

| | |
|---|---|
| Files | `game/WorldSports.uproject`, `Config/*.ini`, `Source/*/*.Build.cs` |
| Depends on | Nothing |
| Tests | Editor build; `RunUAT BuildCookRun` for Android |
| Acceptance | Engine matches `EngineAssociation`; both targets build; an installable APK exists |

Engine 5.8.1 matches. `WorldSportsEditor Win64 Development` builds clean.
`BuildCookRun -platform=Android -cookflavor=ASTC` produces
`WorldSports-arm64.apk` and `WorldSports-x64.apk`.

Two traps are recorded in `TOOLCHAIN_SETUP.md` and cost real time:
`Build.bat` produces an APK with **no cooked content**, and editing sources
while a package is running fails the run with a null-character error in a
generated header that has no null character in it.

## M2 — Race scene verification · **DONE**

| | |
|---|---|
| Files | `Race/WSSprintTrack.cpp`, `Content/Sports/Athletics/Sprint100/L_Sprint100.umap` |
| Tests | `WorldSports.Race.TrackCoversEveryEvent` |
| Acceptance | The world is at least as long as the longest event in ANY table |

The level is empty; the world is built at runtime from procedural boxes.
The length constant has been too short **three times** (100m, then 400m,
then 1500m when the 1600m relay arrived). It is now a test: every event
table is read and the track must cover the longest. The third time was
caught by that test rather than by a runner vanishing into black nothing.

## M3 — Athlete / runner · **DONE (visually placeholder)**

| | |
|---|---|
| Files | `Race/WSSprintRunner.cpp/.h` |
| Acceptance | One actor per athlete, driven only by stepping a simulation |

Nothing may write a position or a time directly. The actor carries a
sprint, pace or relay simulation, or is driven externally for a field
event. Body is a capsule, head a sphere, with a procedural stride bob and
lean. **Skeletal animation is M10 and is not started.**

## M4 — Start and countdown · **DONE**

Set → gun with a randomised pause, drawn from the race seed; the "set" call
offset is drawn independently so a macro cannot time the start off it.
False start is rule-accurate at sub-100 ms, client and server.

## M5 — Input · **DONE**

Hold in the blocks, release on the gun, tap the cadence, swipe to lean.
Field events replace the cadence with a single decision (takeoff, release)
and relays add PASS. The PASS button exists **only** inside the takeover
zone, because a press outside it is a disqualification — the one place in
the game where a control is withheld rather than merely unhelpful.

## M6 — Sprint simulation · **DONE**

Fixed-step 120 Hz, deterministic, seeded. Cadence *accuracy* decides speed,
never tap count. Tested at seven attribute levels against the server's
exact ceiling formula.

## M7 — AI opponents · **DONE**

Seven rivals across five difficulty tiers, each running the same
simulation from its own generated input trace. Nothing teleports.

## M8 — Finish and result · **DONE**

Official timing truncates up to the millisecond. Position, time, reaction,
splits, personal best, XP. A false start produces no time and no
classification for the field — listing seven athletes still in their blocks
as a finished race would be fabricated.

## M9 — HUD · **DONE**

Native Slate, polled per paint. Event-aware: it hides a reaction where
there are no blocks, a wind where the sport records none, a stamina bar
where there is no stamina model, and a race clock where there is no race.
Ten of the nineteen emulator-found defects were this HUD saying something
untrue.

## M10 — Animation · **OPEN**

| | |
|---|---|
| Files | *(none yet)* — needs `Content/Characters/`, `ABP_Athlete`, `BS_Locomotion` |
| Depends on | A skeletal mesh, which does not exist |
| Implementation | Locomotion state machine (Idle → Set → Drive → Sprint → Fatigue → Finish → Celebrate), a speed/fatigue blend space, montages for the start and the lean, foot IK on the track plane |
| Tests | An automation test that every state the game mode can broadcast has a bound animation, so a new phase cannot silently animate as the old one |
| Acceptance | No robotic transitions; the runner visibly responds to acceleration, fatigue and the finish; nothing animates a state the simulation is not in |

**This is the top priority.** Nothing else visual matters while the
athletes are capsules. It is blocked on content only.

## M11 — Camera · **PARTIAL**

Working: a chase rig in `UpdateCamera` with per-event offsets, and
`AWSCameraDirector` mapping phases to named rigs.
Missing: the rigs. Pre-race, starting-block, finish, celebration and replay
shots need to be placed in a level that currently has nothing in it.
Acceptance: broadcast feel, without cinematic cuts during active play.

## M12 — Audio · **OPEN**

The director contract exists (`NotifyPhase`, `AddCrowdExcitement`) and
`WSSprintAudio` already drives it. **No sound assets exist**, so nothing is
audible. Needs: menu music, stadium ambience, crowd (excitement-driven),
starter, footsteps, finish, victory, UI feedback. Placeholders are fine.

## M13 — Progression · **DONE**

Career athlete with server-owned attributes, seven career stages, training
drills whose gains diminish quadratically toward a ceiling and are capped
per rolling day. Attributes raise the ceiling; execution decides the race.

## M14 — Backend result submission · **DONE**

Sixteen events accepted end to end against the live server. Validation:
plausible envelope, reaction bounds, split coherence, wind legality, and
the attribute ceiling. Idempotent by `client_ref`; a persisted offline
queue survives a restart.

## M15 — Leaderboard · **DONE**

Server-side, direction-aware (lower is better for a time, higher for a
mark), scoped global/country. Validated results only.

## M16 — Save / cloud · **DONE**

Local and cloud with a real merge and conflict resolution. `total_xp` is
deliberately not monotonic-merged so a client cannot launder XP.

## M17 — Mobile optimisation · **PARTIAL**

`UWSDeviceProfileSubsystem` defines the tiers. **Nothing has been
profiled.** The emulator cannot judge frame rate, and the last real-device
run (realme C73 5G, 2026-08-16) predates nine of the sixteen events.
Acceptance: 60 FPS on a capable mid-range device, with a playable low tier;
profile before optimising.

## M18 — Final QA · **OPEN**

Blocked on M10–M12 and M17. Also needs the two playtest questions a human
has to answer, which no test can:

1. Is the long jump's sub-half-second takeoff window fair on a phone?
2. Are the triple jump's 0.18 s transition windows learnable as a rhythm?

---

## What happens next, in order

1. **M10** — skeletal mesh and locomotion. Everything visual is behind it.
2. **M11/M12** — camera rigs and audio, both against contracts that exist.
3. **M17** — profile on real hardware, then optimise what the profile shows.
4. **M18** — playtest, then close the Definition of Done.

Sports beyond athletics stay closed until M18 closes. The architecture is
already extensible — the discus, javelin, pole vault and both hurdles races
were added as **table rows**, which is the framework claim paying out four
times.
