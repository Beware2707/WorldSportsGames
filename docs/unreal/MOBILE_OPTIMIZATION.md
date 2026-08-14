# Mobile Optimization

Android-first. The target is a **stable frame rate on mid-range hardware**, not
maximum fidelity on a flagship. A game that stutters on a 3-year-old phone has
failed regardless of how it looks on a Pixel 9.

## 1. Device tiers

| Tier | Rough hardware | Target | Resolution |
|---|---|---|---|
| Low | Snapdragon 6xx / Helio G-series, 3–4 GB | 30 fps locked | 0.6–0.7 × screen |
| Medium | Snapdragon 7xx / Dimensity 800, 6 GB | 30 fps, headroom | 0.8 × |
| High | Snapdragon 8xx / Dimensity 9000, 8 GB | 60 fps | 0.9–1.0 × |
| Ultra | Current flagship, 12 GB+ | 60 fps + effects | 1.0 × |

Detection at first launch: GPU family + RAM + a short calibration frame-time
sample. Store the choice, let the player override, and **re-evaluate if sustained
frame time degrades** — thermal throttling on mobile is the norm, not the
exception, and a profile chosen in the first 30 seconds will be wrong by minute
ten.

## 2. Budgets (per frame, Low tier)

| Resource | Budget |
|---|---|
| Draw calls | ≤ 250 |
| Triangles | ≤ 500 k |
| Skeletal meshes | ≤ 8 visible athletes |
| Bones per athlete | ≤ 60 (LOD0), ≤ 25 (LOD2) |
| Texture memory | ≤ 400 MB |
| Dynamic lights | 1 directional, no dynamic shadows below Medium |
| Niagara systems | ≤ 6 concurrent |
| Game thread | ≤ 12 ms |

Budgets are worthless unmeasured. Wire a CI-visible stat capture on a reference
device from the first playable build — not at the optimization phase, by which
point every violation is load-bearing.

## 3. Rendering choices

- **Forward renderer + MSAA** on mobile. Deferred is not the right trade here.
- **Baked lighting** for the stadium. The stadium doesn't move; paying for
  dynamic GI is pure waste.
- **One dynamic light** (sun) with cascaded shadows only on High+.
- **No Lumen / no Nanite** on Low/Medium. They are not mobile-appropriate at
  these tiers; enabling them "because 5.x has them" is exactly the blind
  feature-enabling §28 warns against.
- **Material complexity capped** — a small set of master materials with
  instances. Per-asset shaders are how mobile projects die.
- **Instanced crowds**: low-poly cards or ISM with baked animation, never
  individual skeletal meshes.

## 4. Characters

The single biggest cost — 8 athletes on screen simultaneously.

- Modular meshes merged at runtime into one skeletal mesh per athlete.
- Aggressive LOD: full skeleton only for the camera-focused athlete.
- **Animation budget allocator** so distant athletes update at reduced rate.
- Shared skeleton and anim assets across every athlete.
- Cloth/physics only on High+.

## 5. Loading

- Async asset loading behind the loading screen; never block the game thread.
- Level streaming for stadium sections.
- **Preload the next race during the results screen** — the player is reading,
  so the load is free.
- Hard requirement: cold start to playable ≤ 15 s on Low tier.

## 6. Memory

- Texture streaming with a per-tier pool.
- ASTC compression on Android.
- Object pooling for athletes, VFX, HUD widgets.
- Unload the previous sport's content when switching.
- Watch for OOM kills on 3 GB devices — they are silent and look like crashes.

## 7. Network

- Batch analytics; never per-event HTTP.
- Result submission is small, retried with backoff, and queued offline.
- Cache the catalogue (sports, nations) locally; it changes rarely — the
  backend already serves it through a fail-open Redis cache.
- Assume flaky connectivity: every online call must have a defined offline
  behaviour, not a spinner.

## 8. Profiling discipline

**Profile before optimizing** (§24). Order of investigation:

1. `stat unit` — is it game, draw, or GPU bound?
2. `stat scenerendering` / RenderDoc for draw-bound.
3. Unreal Insights for game-thread spikes.
4. Android GPU Inspector for GPU-bound.
5. Only then change something, and re-measure.

Ship a `stat` overlay behind a debug flag in internal builds so any tester can
capture a regression rather than describing it as "feels laggy".
