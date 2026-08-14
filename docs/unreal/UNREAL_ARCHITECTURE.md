# Unreal Architecture

Target: Unreal Engine 5.x (confirm the current stable release before pinning —
see `MIGRATION_ANALYSIS.md` §0), C++ core with Blueprint composition,
Android-first.

## 1. Module layout

One game module plus separable feature modules, so a sport can be added without
touching the core and so build times stay sane.

```
Source/
  WorldSports/                 primary game module
    Core/                      subsystems, config, logging
    Framework/                 GameMode/State/PlayerController/Pawn bases
    Sports/                    sport framework (NOT individual sports)
      Definitions/             USportDefinition, UEventDefinition data assets
      Scoring/                 scoring strategies per result kind
      Results/                 FEventResult, submission pipeline
    Characters/                modular athlete, appearance, attributes
    Animation/                 AnimInstance bases, state machine contracts
    Camera/                    reusable camera rigs and director
    AI/                        AI athlete controller, difficulty profiles
    UI/                        UMG bases, HUD framework, view models
    Audio/                     audio subsystem, crowd/announcer buses
    Save/                      local save + cloud sync + conflict resolution
    Online/                    HTTP/WS client, DTOs, auth, retry/backoff
    Analytics/                 event queue and dispatch
  WorldSportsAthletics/        FIRST sport module — 100m lives here
  WorldSportsEditor/           editor-only tooling, data validation commandlets
```

```
Content/
  Core/            shared materials, master shaders, common FX
  Characters/      modular meshes, skeleton, anim assets, materials
  Sports/
    Athletics/
      Sprint100/   level, cameras, HUD, sport data assets
  UI/              widget blueprints, styles, fonts
  Audio/           cues, banks, buses
  Environments/    stadium kit, crowd, props
  FX/              Niagara systems
```

**Rule:** `WorldSports` never `#include`s a sport module. Sports depend on the
core, never the reverse. If the core needs to know about a sport, that is a
missing abstraction, not a reason to add a dependency.

## 2. C++ vs Blueprint

| C++ | Blueprint |
|---|---|
| Gameplay simulation & scoring | Animation state machines |
| Data assets & definitions | UI wiring and transitions |
| Save/load, cloud sync | Level scripting |
| HTTP/WS, DTOs, auth | VFX/audio triggers |
| Subsystems, AI decisions | Designer-tunable curves |
| Anything needing an automated test | Rapid prototyping |

Anything that determines a **submitted result** is C++ and unit-tested. A
Blueprint-only scoring path cannot be tested in CI and cannot be trusted.

## 3. Core subsystems (`UGameInstanceSubsystem`)

| Subsystem | Responsibility |
|---|---|
| `UOnlineSubsystem_WS` | HTTP/WS to FastAPI: auth token lifecycle, retry with backoff, offline queue |
| `USaveSubsystem` | Local save, cloud sync, conflict resolution |
| `UProgressionSubsystem` | XP/level/attributes mirror — **display only**, server is authoritative |
| `USportRegistrySubsystem` | Resolves `USportDefinition` assets by id |
| `UAudioDirectorSubsystem` | Music/crowd/announcer state |
| `UAnalyticsSubsystem` | Buffered event dispatch |
| `UDeviceProfileSubsystem` | Detects tier, applies scalability (see `MOBILE_OPTIMIZATION.md`) |

Subsystems, not singletons or a god `GameInstance`: lifetime is engine-managed
and each is independently testable.

## 4. Gameplay framework per event

```
AWSGameModeBase            owns the event lifecycle (server of truth locally)
  └ AWSEventGameMode       one per event *kind*, not per sport
AWSGameStateBase           replicated race state, phase, clock
AWSPlayerController        input routing, camera possession, HUD ownership
AWSAthleteCharacter        modular athlete pawn (player or AI)
AWSAthleteAIController     AI opponents through the SAME simulation
UWSEventRules (DataAsset)  tunables: distances, curves, wind, scoring
```

**AI opponents run the identical simulation as the player** — different input
source, same physics and scoring. The brief (§36) forbids teleporting AI to a
predetermined time, and sharing the simulation is what structurally prevents it.

## 5. Event lifecycle (sport-agnostic)

```
Load → Present → Ready → Active → Finishing → Result → Submit → Reward
```

Each phase is a state on `AWSEventGameMode`. The 100m maps
`Ready→Set→Go→Accelerate→TopSpeed→Fatigue→Finish` onto `Ready`/`Active`;
a match sport maps periods onto the same states. Camera, HUD and audio all
subscribe to phase changes rather than polling.

## 6. Result submission path

```
Local simulation
  → FEventResult (time, splits, reaction, input trace digest, RNG seed)
  → USaveSubsystem persists locally FIRST (never lose a result)
  → UOnlineSubsystem submits
  → Backend validates (plausibility + attribute ceiling)
  → Backend returns authoritative XP/level/PB/rank
  → UProgressionSubsystem updates display from the RESPONSE
```

The client never computes its own XP. This mirrors what the existing backend
already enforces for the 2D games and satisfies §23.

An **offline queue** is mandatory: a result recorded with no connectivity is
persisted and submitted on reconnect. The existing `dedupe_key` pattern in the
notification service is the right shape for making resubmission idempotent.

## 7. Networking

Phase 1 is **async competition only** — HTTP result submission plus
leaderboards. The existing WebSocket (`/api/v1/live/ws`) is reused for live
leaderboard/tournament updates, not for gameplay.

Real-time PvP is deferred (§21 of the brief agrees). When it lands it needs a
dedicated authoritative server; do not attempt it with listen servers on mobile.

## 8. Testing

- **C++ automation tests** for scoring, timing, fatigue curves, save/load,
  DTO round-trips, offline queue. These run headless in CI.
- **Functional tests** for the event lifecycle on a stripped level.
- **Backend pytest** continues to cover validation and leaderboards.
- **On-device** smoke on a real mid-range Android handset before each release.

A gameplay formula without a test is a formula that will silently drift.
