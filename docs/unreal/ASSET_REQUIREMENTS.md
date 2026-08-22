# Asset Requirements

`game/Content/` currently holds **two files**: `.gitkeep` and an empty
`L_Sprint100.umap`. Everything visible in the game is procedural geometry
built in C++ at runtime. That was a deliberate choice — it kept sixteen
events playable with zero art dependency — and it is now the single
constraint on everything visual.

This document lists exactly what is needed, so the work can be sourced or
commissioned without further archaeology. **Nothing here may come from the
reference game** (`com.gamebee.worldsports`), and no Olympic marks,
rings, or the word "Olympic" may appear anywhere.

Priority: **P0** blocks the Definition of Done. **P1** is the next
noticeable step up. **P2** is polish.

---

## 1. Athlete character — P0

The largest gap. Athletes are a capsule and a sphere.

| Asset | Spec |
|---|---|
| Skeletal mesh | One athletic body, ~15–25k tris for mobile, 3 LODs |
| Skeleton | UE5 Mannequin-compatible if possible — retargeting is free then |
| Body variation | Height/build morph targets or 2–3 mesh variants |
| Kit | Vest and shorts as a separate material slot, tintable per lane (the code already assigns a per-lane kit colour) |
| Skin tones | Material parameter, not separate textures |
| Textures | 1024² albedo/ORM per material, ASTC-friendly |

**Animations required.** The state machine in M10 needs, at minimum:

- `Idle`, `WalkToBlocks`, `IntoBlocks`, `SetPosition`
- `BlockDrive` (first ~10 m, low body angle), `Accelerate`, `Sprint`
- `SprintFatigued` (for the 400m and the middle-distance events)
- `Lean` (a montage, fired at the line)
- `Decelerate`, `Celebrate`, `Dejected`
- Field events: `RunUp`, `JumpTakeoff`, `Hop`, `Step`, `Jump`, `Flop`
  (high jump), `Vault`, `ThrowWindUp`, `ThrowRelease`
- Relay: `Handover` (giving), `HandoverReceive` (taking, looking away)

A locomotion **blend space** on speed × fatigue covers most of it; the rest
are montages.

## 2. Stadium — P1

Currently: a box for the track surface, boxes for lane lines, a box
backdrop and two side walls.

| Asset | Spec |
|---|---|
| Track surface | Tiling material, lane lines in the texture rather than as geometry |
| Infield | Grass material |
| Seating | Modular tiers, instanced |
| Crowd | Impostor cards or a low-poly instanced crowd; must scale to the low tier |
| Lighting | Baked where possible; four floodlight towers |
| Timing board | A mesh with a render-target face — the times already exist |
| Advertising panels | **Fictional brands only.** Original artwork |
| Flags | Country flags for the athlete's nation. Generic pennants otherwise |
| Podium | For the tournament medal moment |

## 3. Event furniture — P1

These exist as procedural boxes and work. Replacing them is polish, with
one rule learned the hard way:

> **Anything the player must AIM at is drawn oversized on purpose.**
> A regulation 30 mm crossbar is one pixel from the far end of a 40 m
> runway. Keep the exaggeration when the meshes land.

Starting blocks · hurdles (must fall) · takeoff board · sand pit (needs a
displacement or decal for the mark) · high jump uprights, crossbar and
landing mat · pole vault equivalents plus a pole · throwing circle · javelin
runway and foul arc · relay takeover zone lines · finish line and posts.

## 4. Audio — P0

The director contract exists (`UWSAudioDirectorSubsystem::NotifyPhase`,
`AddCrowdExcitement`) and `WSSprintAudio` already drives it. **No sound
files exist**, so the game is silent.

| Cue | Notes |
|---|---|
| Menu music | Loop |
| Stadium ambience | Loop |
| Crowd | Layered by the existing 0–1 excitement value |
| Starter | "On your marks", "Set", gun. A recall gun for a false start |
| Footsteps | Surface-appropriate, rate-driven by stride |
| Breathing | Fatigue-driven, for the long events |
| Implement | Shot landing, discus, javelin, bar rattle, bar falling, mat |
| Finish | Line crossing, announcer stinger |
| Victory / defeat | Short |
| UI | Tap, confirm, back, error |

Placeholders are fine and should land first. Audio must never block
gameplay work.

## 5. UI — P1

The HUD is native Slate and is functionally complete. What is missing is
the **look** and the reusable component set.

Needed: a type scale and palette; button/panel/card components; a main-menu
background; event icons for sixteen events; medal and rank-change art; a
results-reveal animation; country flag icons; an attribute-radar or bar
component for the athlete screen.

## 6. VFX — P2

Dust at the blocks, sand displacement on landing, a speed-line or
motion-blur cue at top speed, confetti on a medal, a bar-rattle particle.

---

## Rules for whoever sources this

1. **Original or properly licensed only.** Nothing from the reference game
   — not source, assets, characters, animations, sounds, music, UI, logos,
   branding or levels.
2. **No Olympic branding.** No rings, no torch, no "Olympic".
3. **Mobile budgets first.** ASTC textures, 3 LODs, instancing for anything
   repeated, and a low tier that actually runs.
4. **Mark placeholders.** Anything temporary goes under
   `Content/_Placeholder/` so it cannot be mistaken for final.
5. **Do not block gameplay on assets.** The procedural world stays until a
   replacement is better, and every replacement keeps the exaggerated scale
   of anything the player aims at.
