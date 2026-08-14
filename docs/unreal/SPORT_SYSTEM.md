# Sport System

The architecture must absorb ~60 sports across 7 phases without the core being
rewritten. That means **a sport is data plus a small strategy implementation —
never a branch in a shared GameMode.**

This is the same principle the existing backend already proved: its 8 mini-games
are rows in a table with an `engine` name, and adding a game requires no schema
change. The 3D game keeps that shape.

## 1. Definition hierarchy

```
USportDefinition          Athletics, Aquatics, Archery…
  └ UDisciplineDefinition   Track & Field, Marathon, Swimming…
      └ UEventDefinition      100m, 200m, Long Jump, 200m Freestyle…
           ├ EventKind         which gameplay/scoring family
           ├ URulesDataAsset   distances, curves, wind, legality
           └ UPresentationSet  cameras, HUD layout, audio, celebration
```

All four are `UPrimaryDataAsset` — async-loadable, cookable, and editable
without a code change. Tunables (§41) live in curves and data tables so design
iterates without a programmer.

## 2. Event kinds

The load-bearing abstraction. Every sport implements **one** of these; the core
knows only these, never a sport name.

| Kind | Result | Wins by | Examples |
|---|---|---|---|
| `Timed` | duration | lower | sprints, swimming, cycling TT, luge |
| `Distance` | metres | higher | long/triple jump, javelin, shot |
| `Height` | metres + attempts | higher, then fewer failures | high jump, pole vault |
| `Accuracy` | points | higher | archery, shooting |
| `Judged` | score | higher | gymnastics, diving, figure skating |
| `Match` | goals/sets | opponent-relative | football, tennis, boxing |
| `RoundBased` | aggregate | ruleset | golf, decathlon, biathlon |

This maps **exactly** onto the backend's existing `RESULT_VALUE_KINDS`
(`time | distance | height | points | score | goals | sets | rounds`). The
server already stores results this way, so an Unreal event kind serialises into
the existing `Result` model with no schema change. That alignment is not a
coincidence worth losing — keep the two vocabularies in sync.

## 3. Scoring strategies

```cpp
class IEventScoringStrategy
{
public:
    virtual FEventResult  Score(const FEventSimulationState&) const = 0;
    virtual bool          IsBetter(const FEventResult& A, const FEventResult& B) const = 0;
    virtual FText         Format(const FEventResult&) const = 0;
};
```

`IsBetter` exists because **direction is per event, not global** — the backend
learned this the hard way (`score_direction` per game). A timed event improves
downward; a distance event upward; golf strokes downward again. Any code that
hardcodes "higher is better" is a bug waiting for the first throwing event.

## 4. Adding a sport

1. Author `USportDefinition` / `UDisciplineDefinition` / `UEventDefinition`.
2. Pick an existing `EventKind`, or add one **only if genuinely new**.
3. Provide rules data (curves, distances, legality).
4. Provide presentation (cameras, HUD, audio, celebration).
5. Implement an input handler if the mechanic is new; otherwise reuse.
6. Register in the sport module's `.uplugin`/module registry.

Steps 1, 3, 4 are data. Only step 5 is code, and only for genuinely new
mechanics. Adding the 200m after the 100m should be **data only**.

## 5. Reuse across the roadmap

Phase 2 (athletics) is where the investment pays off:

- **200m/400m** — `Timed`, same input, different curves + bend camera. Data.
- **800m/1500m** — `Timed` + pacing/tactics input. Small code addition.
- **Hurdles** — `Timed` + rhythm-gate mechanic. Moderate.
- **Relays** — `Timed` + baton exchange. Moderate.
- **Jumps** — `Distance`/`Height`: approach → takeoff → flight. New input, reused
  camera/result/progression.
- **Throws** — `Distance`: wind-up → release angle/power. New input, reused rest.
- **Decathlon** — `RoundBased` aggregating the above. Almost entirely reuse.

Phase 3+ (aquatics, archery, cycling…) reuse `Timed`/`Accuracy` wholesale.
Match sports (`Match`) are the genuinely large new investment — they need
opponent AI, ball physics and team logic, and should be scheduled accordingly
rather than assumed cheap.

## 6. Anti-patterns

- ❌ `if (SportName == "100m")` anywhere in the core.
- ❌ A GameMode subclass per event.
- ❌ Duplicated camera/HUD/result code per sport.
- ❌ Tunables hardcoded in C++.
- ❌ Scoring logic living only in a Blueprint (untestable).
- ❌ Assuming higher-is-better.
