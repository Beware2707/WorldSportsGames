# Current Implementation — Audit

**Audited:** 2026-08-22, against the working tree (branch `main`).
**Method:** every claim below was checked against the code, the build, the
test suites or a device run. Where documentation and code disagreed, the
code won. Line counts are of `.cpp` under each directory.

> **Read this first.** The brief that requested this audit describes the
> 100m sprint as the objective still to be reached. It has been reached.
> The 100m vertical slice was completed several phases ago and the project
> is now sixteen athletics events deep, all server-validated and all
> verified on an Android emulator. The genuine gaps are no longer gameplay
> gaps — they are **content** gaps. See §17 and §20.

---

## 1. Engine and project configuration

| Item | Value | Verified |
|---|---|---|
| Engine | Unreal Engine **5.8.1**, `C:\Program Files\Epic Games\UE_5.8` | Builds and runs |
| `EngineAssociation` | `"5.8"` — matches the installed engine | `game/WorldSports.uproject` |
| Plugins | **EnhancedInput** only | `.uproject` |
| Default map | `/Game/Sports/Athletics/Sprint100/L_Sprint100` | `Config/DefaultEngine.ini:2` |
| Android SDK/NDK | platform-34, NDK 27.2.12479018, build-tools 35.0.1 | `docs/unreal/TOOLCHAIN_SETUP.md` |
| JDK | Temurin **21**.0.12.8 (Gradle 8.7 cannot run Java 25) | Packaging succeeds |
| Android ABIs | arm64 **and** x86-64 (`bBuildForX8664=True`, for the emulator) | Both APKs produced |

No engine-version change has been made or is needed.

## 2. Modules

Three, exactly as the brief describes:

| Module | Type | Dependencies |
|---|---|---|
| `WorldSports` | Runtime | Core, CoreUObject, Engine, InputCore, EnhancedInput, UMG, AIModule, DeveloperSettings, HTTP, Json, JsonUtilities, Slate, SlateCore, AssetRegistry |
| `WorldSportsAthletics` | Runtime | + `WorldSports` |
| `WorldSportsEditor` | Editor | + UnrealEd |

Athletics-specific code is confined to `WorldSportsAthletics`. The generic
sport layer in `WorldSports/Sports` has no athletics in it.

## 3. Major C++ systems — what is real and what is a header

Measuring implementation rather than intent:

| System | Headers | `.cpp` LOC | State |
|---|---|---|---|
| `Athletics/Race` | 6 | **5,730** | Complete. Game mode, HUD, track, runner, controller, audio |
| `Athletics/Simulation` | 7 | **2,686** | Complete. Seven event kinds, all calibrated |
| `Online` | 3 | 635 | Complete. Auth, requests, submit, offline queue |
| `Progression` | 2 | 592 | Complete. Career athlete, attributes, tournaments |
| `Save` | 2 | 313 | Complete. Local + cloud, merge, conflict |
| `Framework` | 7 | 160 | Thin but sufficient — phases, rules, base classes |
| `Analytics` | 1 | 126 | Batching client; **event set is minimal** |
| `Sports` | 4 | 69 | Definitions/results/scoring/registry — data-only, correct |
| `Core` | 3 | 43 | Log, device profile |
| `Camera` | 1 | **32** | **Skeleton.** Phase→rig map; no rigs exist |
| `UI` | 1 | **27** | **Skeleton.** Base widget only; the real HUD is Slate in Race |
| `AI` | 2 | **0** | **Headers only.** No `.cpp` file exists |

**The `AI/` directory is a skeleton and has been all along.**
`WSAthleteAIController.h` and `WSDifficultyProfile.h` have no
implementation. This is not a defect: the AI that actually runs is
`FWSSprintDifficulty::Levels()` plus each simulation's `GenerateAITrace()`,
which produces an input trace that the *same* simulation then executes.
Rivals are not controllers and do not need to be. The empty `AI/` headers
should be deleted or implemented; leaving them implies a system that is not
there.

## 4. Existing 100m functionality — complete

Every stage of the loop the brief specifies works and is covered by a test:

MAIN MENU → EVENT SELECT → PRE-RACE (set/gun with a randomised pause) →
BLOCKS/REACTION → RACE (cadence) → FINISH (lean) → RESULTS → XP → PERSONAL
BEST → LEADERBOARD → SAVE → MENU

- **False start** is rule-accurate: sub-100 ms is a false start client- and
  server-side, the race is recalled, and no time is submitted.
- **Splits** are per-event, not hardcoded (a 400m splits every 50 m).
- **The lean** at the line is a real decision with a real cost.
- **Anti-macro:** the set-to-gun interval is drawn per race from the seed
  and the "set" call offset is drawn independently, so a constant-offset
  macro cannot score a perfect reaction.

## 5. AI opponents

Seven rivals per race, spread across `FWSSprintDifficulty::Levels()`.
Each rival runs **the same simulation as the player** from a trace
generated for its consistency/reaction profile. Nothing teleports to a
predetermined time. Tournament rivals are the one exception and are
honest about it: their times come from the server, which stored the field
before the race and scores the player against those exact times —
simulating them locally would put a different race on screen from the one
being scored.

## 6. Simulation — seven event kinds

All fixed-step at 120 Hz, deterministic, seeded, replayable from an input
trace.

| Kind | Class | Events |
|---|---|---|
| Sprint (cadence) | `FWSSprintSimulation` | 100m, 200m, 400m |
| Sprint + barriers | same, data-driven | 110mH, 400mH |
| Middle distance (effort/energy) | `FWSMiddleDistanceSimulation` | 800m, 1500m |
| Field — horizontal jump | `FWSJumpSimulation` | Long jump |
| Field — vertical ladder | same, `bVertical` | High jump, pole vault |
| Field — multi-phase jump | same, `PhaseCount=3` | Triple jump |
| Field — throw | `FWSThrowSimulation` | Shot, discus, javelin |
| Relay | `FWSRelaySimulation` | 4x100m, 4x400m |

**The calibration invariant**, asserted for every event at seven attribute
levels: perfect play *approaches* the server's ceiling and never beats it.
Every per-attribute effect is capped at the mean the ceiling is computed
from, which makes "no attribute beats the ceiling" structural rather than
retuned.

## 7. UI

Native **Slate**, one panel (`SWSSprintHudPanel`), polled per paint from the
game mode. No UMG widget blueprints exist. The HUD is event-aware: it hides
a reaction where there are no blocks, a wind where the sport records none,
a stamina bar where there is no stamina model, and a race clock where there
is no race.

There is **no main-menu art, no results-screen animation and no reusable
widget library**. The menu is Slate buttons. This is the largest UI gap.

## 8. Camera

`AWSCameraDirector` maps event phases to named camera rigs placed in the
level. **No rigs are placed**, because the level has no content. The race
camera that actually runs is code in `AWSSprintGameMode::UpdateCamera` —
a chase rig with per-event offsets. Broadcast-style pre-race, finish,
celebration and replay cameras do not exist.

## 9. Audio

`UWSAudioDirectorSubsystem` is 15 lines: a phase notifier and a crowd
excitement value that decays. `WSSprintAudio` binds race events to it.
**No sound assets exist**, so nothing is audible. The contract is there for
content to bind to.

## 10. Save

Local `USaveGame` plus cloud save with a real merge: monotonic keys grow,
list keys union, and anything else is a genuine divergence the client must
resolve. `total_xp` is deliberately **not** monotonic-merged — a client
must not be able to launder XP through a save.

## 11. Online

`UWSOnlineSubsystem`: auth, typed requests, result submission with
idempotency (`client_ref`), and a **persisted offline queue** that survives
a restart and flushes in order. Submission outcomes are classified
(`Accepted` / `Rejected` / `Queued` / failed) and a rejection never looks
like a network problem.

## 12. Progression

`UWSProgressionSubsystem` (career athlete, server-owned attributes) and
`UWSTournamentSubsystem` (qualification → semifinal → final, scored
server-side). Attributes are read by lookup, never by iterating
`FJsonObject` — its key type is not `FString` on Android.

## 13. Analytics

`UWSAnalyticsSubsystem` batches and POSTs. **The named event set the brief
asks for (GameStarted, FalseStart, PersonalBest, LevelUp, …) is not wired
up.** The transport exists; the call sites mostly do not.

## 14. Assets and maps

**`game/Content/` contains exactly two files:** `.gitkeep` and
`L_Sprint100.umap` (6 KB — an empty level).

Everything visible in the game is **procedural geometry built in C++**:
the track surface, lane lines, starting blocks, hurdles, the takeoff board,
the sand pit, the crossbar and uprights, the landing mat, the throwing
circle, the javelin arc, the takeover zones, the backdrop, and the athletes
themselves (a capsule and a sphere). There are no meshes, materials,
textures, animations, animation blueprints, blend spaces, montages,
particles or sounds.

This is a deliberate placeholder strategy that has worked — it kept the
game playable with zero art dependency — and it is now the binding
constraint on everything visual.

## 15. What compiles and what is tested

| Suite | Count | Result |
|---|---|---|
| Unreal automation (offline) | **60** | All pass |
| Unreal automation (live backend) | **7** | All pass, 16 events accepted end to end |
| Backend `pytest` | **227** | All pass |
| Android package (arm64 + x86-64) | — | Succeeds |
| Emulator run | — | Every event played to a result |

`WorldSportsEditor` builds; the editor opens the project; `L_Sprint100`
loads (and is empty, as above — the world is built at runtime).

## 16. What is incomplete

- Animation: **nothing**. Runners translate and bob; there is no skeleton.
- Camera rigs, broadcast shots, replay camera.
- Audio assets.
- Main menu, results-screen presentation, reusable widget library.
- Analytics call sites.
- `AI/` headers with no implementation.
- Athlete customisation (name and country only; no appearance/equipment).

## 17. What is placeholder

All of `game/Content`. Every mesh in the game is `MakeBox` or a capsule.
Furniture the player must **aim** at is drawn deliberately oversized —
a regulation 30 mm crossbar is one pixel from the far end of a 40 m runway.

## 18. What is broken

Nothing known. Eight defects found by an adversarial review on 2026-08-22
were fixed the same day and each has a regression test:

| Was | Now |
|---|---|
| Relay leg splits included the reaction; the server refused the result | Legs measure running time; verified against the live server |
| A duplicate PASS press disqualified the team | Half-second handover lockout |
| The triple jump's miss penalty leaked into later phases | Charged to its own phase, isolated by a closed-loop test |
| High jump and pole vault claimed a wind reading | `FieldResultHasWind()`, asserted per event |
| A post-round tournament refresh could be dropped | `bRefreshAgain` re-reads |
| An unparseable 200 silently cleared the bracket | Parse failure is a failure |
| Training's retry path could raise on a NULL `client_ref` | Guarded, with a test |
| Relay passes used a different clock from taps | One clock; trace order asserted |

## 19. What should be fixed first

In order of what blocks the most:

1. **Animation.** A skeletal mesh with a locomotion state machine. Nothing
   else in the visual list matters while athletes are capsules.
2. **Stadium content.** Track, seating, crowd, lighting, timing board.
3. **UI pass.** Menu, results reveal, a reusable component set.
4. **Audio assets** against the existing director contract.
5. Analytics call sites, then delete-or-implement the `AI/` headers.

## 20. Exact 100m implementation gap

**Gameplay: none.** Measured against the brief's own Definition of Done,
every gameplay, backend, save, offline and packaging box is met.

**Presentation: four boxes remain**, and all four are content, not code:

| Definition-of-Done item | State |
|---|---|
| Animation transitions work | **No** — no skeletal mesh exists |
| Audio works | **No** — no sound assets exist |
| Camera works | **Partly** — a working chase rig; no broadcast shots |
| Low/medium/high quality settings work | **Partly** — `UWSDeviceProfileSubsystem` tiers exist; unprofiled |

One further item cannot be closed on an emulator: **60 FPS on real
mid-range Android hardware**. The emulator cannot judge frame rate. The
last measured on-device run was a realme C73 5G (2026-08-16), before nine
of the sixteen events existed.
