# Game Design

## 1. Design pillars

**Fun in 30 seconds. Mastery over months.** A first-time player should finish a
100m race within seconds of opening the app and understand exactly why they got
the time they did. A skilled player should be chasing hundredths a hundred races
later.

Four constraints hold everything else:

1. **One thumb.** Everything playable one-handed, portrait or landscape.
2. **Legible failure.** A bad race must be explainable — "you left late",
   "you burned out at 70m" — never "the game decided".
3. **Skill decides.** Attributes raise the ceiling; execution decides the race.
   Two identical athletes must be separated by play (§16, §45).
4. **Honest feedback.** Reaction time in milliseconds, splits, wind. Show the
   real numbers.

## 2. The 100m as the benchmark

The 100m is the whole game's technical proof: start → acceleration → top speed →
fatigue → finish, with reaction, timing, camera, replay, results, progression
and leaderboards all exercised. If it feels good, the framework is right.

### Race phases

| Phase | Distance | Player action | Dominant attribute |
|---|---|---|---|
| Set | — | Hold to settle into blocks | — |
| Start | 0 m | Release on the gun | Reaction |
| Drive | 0–30 m | Rhythm taps building cadence | Acceleration |
| Transition | 30–60 m | Maintain rhythm, rise upright | Technique |
| Top speed | 60–85 m | Hold optimal cadence | Max speed |
| Fatigue | 85–100 m | Manage decay, lean at the line | Stamina |

### Input model (to be validated by playtest)

Primary candidate — **rhythm-and-hold**:

- **Set:** press and hold. Releasing before the gun = false start.
- **Start:** release within the reaction window. Sub-100 ms is a false start
  (rule-accurate and it prevents mash-spamming).
- **Drive→Top speed:** alternating taps in a moving target band. Hitting the
  band builds *stride efficiency*; missing it costs speed and stamina.
- **Fatigue:** the band narrows and drifts. Holding rhythm here is the skill
  ceiling.
- **Lean:** swipe down in the last 5 m — small reward, small risk if early.

Rejected: `speed = taps/second`. It rewards a mechanical device over a player,
destroys the fatigue mechanic, and is unwinnable to balance. The brief (§7)
rules it out explicitly and it is worth restating: **cadence accuracy, not
tap count.**

Two alternates to prototype before committing: swipe-rhythm, and hold-release
per stride. Pick by feel on a real device, not on paper.

### Simulation

Per tick, roughly:

```
target_cadence = f(phase, technique)
accuracy       = 1 - |actual_cadence - target_cadence| / tolerance
drive          = acceleration_attr * accuracy * (1 - fatigue)
speed         += (drive - drag(speed)) * dt
fatigue       += cost(speed, accuracy, stamina_attr) * dt
```

Deterministic given (attributes, input trace, wind seed) — required so the
server can validate a submitted result and so replays reproduce exactly.

## 3. Modes

**Quick Play** — pick event, race, result. No meta required. This is the
30-second promise.

**Career** — create an athlete, train, climb Local → Regional → National →
Continental → World → Championship. Progression gates *competition access*, not
raw power.

**Tournament** — Qualification → Heat → Semifinal → Final → medal ceremony.
Reuses the event framework with a bracket state machine.

**Challenge** — short, sharp goals: beat a time, perfect reaction, streaks.
Cheapest content per unit of engineering; excellent daily-return hook.

**Multiplayer** — async first (ghosts + leaderboards), real-time much later.

## 4. Progression

Rookie → Regional → National → Elite → World Class → Champion → Legend.

Attributes: Reaction, Acceleration, Max Speed, Stride Efficiency, Stamina,
Recovery, Technique.

**Attributes raise the ceiling; they do not win races.** A World Class athlete
played badly must lose to a Regional athlete played well. This is the single
most important balance constraint, and it is what keeps the game from becoming
pay-to-win (§45).

Training mini-games map to attributes: reaction drills, sled pushes (accel),
flying sprints (top speed), tempo runs (stamina), form drills (technique).

## 5. Equipment

Cosmetics are the primary monetization surface. Performance equipment, if any,
is capped at a few percent and **never** sold for money — earned only. The
competitive integrity rule from §18/§45 is non-negotiable.

## 6. Records and presentation

PB / SB / NR / WR, using the record model that already exists in the backend.
A record triggers a distinct celebration and a stored entry.

Broadcast presentation (intro, athlete walk-out, camera sweep, replay, medal
ceremony) is reserved for **finals and records** — everywhere else it is a cost
with no payoff, and it slows the 30-second loop.

## 7. Accessibility

Colour is never the only signal. Timing bands have shape and audio cues.
Configurable input timing windows. Reduced-motion camera option. Text scales.
The Flutter app's accessibility pass found a real WCAG contrast failure — the
same rigour applies here from the start rather than as a late audit.
