# 100m Status — Definition of Done

Checked 2026-08-22 against the brief's own checklist. Every ✅ was verified
by a test, a live-server round trip or a device run — not by reading code.
Evidence is named so any line can be re-checked.

| | Item | Evidence |
|---|---|---|
| ✅ | Game launches | Emulator, package `com.worldsports.game` |
| ✅ | Main menu works | Event selector across 16 events, Quick Play / Career / Tournament / Leaderboard / Account / Settings |
| ✅ | Athlete can be selected | Career athlete created server-side; name, country, gender |
| ✅ | Sprint100 level loads | `L_Sprint100` is the default map; the world is built at runtime |
| ✅ | Starting blocks work | Hold → release; blocks hidden for events that have none |
| ✅ | Countdown works | Set → gun, both intervals drawn from the seed |
| ✅ | False start works | Sub-100 ms, client and server; race recalled, no time |
| ✅ | Mobile controls work | Tap cadence, swipe lean, event-specific buttons |
| ✅ | Sprint simulation works | 120 Hz fixed step, deterministic, calibrated at 7 attribute levels |
| ✅ | AI opponents work | 7 rivals, same simulation, no teleporting |
| ✅ | Race finishes correctly | Millisecond truncation, per-event splits |
| ✅ | Time is calculated | `WorldSports.Race.FullRaceProducesStandings` |
| ✅ | Position is calculated | Standings ordered; a DQ is classified as a DQ |
| ✅ | Results screen works | Position, time, reaction, splits, verdict |
| ✅ | Personal best works | Server-side, direction-aware |
| ✅ | XP works | Server-awarded; a client cannot write it |
| ✅ | Rewards work | XP and career stage from the server |
| ✅ | Career progression works | 7 stages, training drills, server-owned attributes |
| ✅ | Leaderboard submission works | Global and country, validated results only |
| ✅ | Backend validates result | Envelope, reaction, splits, wind, attribute ceiling |
| ✅ | Save works | Local + cloud, merge and conflict resolution |
| ✅ | Offline behavior works | Persisted queue survives a restart, flushes in order |
| ✅ | Reconnect behavior works | `LiveBackend.*` (7 tests) |
| ❌ | **Audio works** | Director contract exists; **no sound assets** |
| ⚠️ | **Camera works** | Chase rig works; no broadcast/finish/replay rigs |
| ❌ | **Animation transitions work** | **No skeletal mesh.** Capsules with a stride bob |
| ⚠️ | Low/medium/high quality settings | Tiers defined; **never profiled** |
| ✅ | Android development build works | arm64 + x86-64 APKs |
| ✅ | No critical crashes | None outstanding |
| ✅ | Tests pass | 60 offline · 7 live · 227 backend |

**24 of 29 met.** The five that are not are M10, M11, M12 and M17 in
[100M_IMPLEMENTATION_PLAN.md](100M_IMPLEMENTATION_PLAN.md), and every one
of them is content, not code.

---

## Beyond the 100m

The slice was closed several phases ago. What exists now is **sixteen
athletics events** across seven simulation kinds, all server-validated:

100m · 200m · 400m · 110mH · 400mH · 800m · 1500m · long jump · high jump ·
triple jump · pole vault · shot put · discus · javelin · 4x100m · 4x400m

The framework claim has paid out repeatedly: the discus, the javelin and
the pole vault were each added as a **table row**, with no new code.

## Two questions only a person can answer

No test can settle these, and they are the last gameplay unknowns:

1. **The long jump's takeoff window is under half a second.** The athlete
   arrives at the board near 9 m/s. Is that fair on a phone?
2. **The triple jump's transition windows are 0.18 s either side of each
   landing.** They are most of a 0.4 s flight and the phases are near
   enough uniform that the rhythm should be learnable — but that is a
   claim, not a measurement.

## The measurement that is missing

**Frame rate on real hardware.** The emulator cannot judge it. The last
on-device run was a realme C73 5G on 2026-08-16, when seven of the sixteen
events existed. Until a device run happens, the 60 FPS target is an
aspiration rather than a result.
