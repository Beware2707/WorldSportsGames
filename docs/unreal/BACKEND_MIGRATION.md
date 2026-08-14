# Backend Migration

**The backend is kept and extended, not rewritten.** ~80% transfers unchanged
(see `MIGRATION_ANALYSIS.md` §3). This document covers only the delta.

## 1. What changes: nothing, for the client swap

JWT bearer auth is transport-agnostic. Unreal's HTTP module sends the same
`Authorization: Bearer <token>` header Dio does. `/api/v1/auth/*`,
`/api/v1/sports`, `/api/v1/countries`, `/api/v1/records`, `/api/v1/rankings`,
`/api/v1/games/*` all work against Unreal with **zero server change**.

The one required change is CORS/network config — Unreal is not a browser, so
CORS is irrelevant to it, but the companion app still needs it. Leave as is.

## 2. New: career athlete (do not overload the existing model)

The existing `Athlete` model is a **real-world athlete** — a catalogue record
with a slug, disciplines, medals and records. The game needs a **player-owned
career athlete**. These are different things and must not share a table:
conflating them would let game progression corrupt the sports catalogue and
would make "list athletes" return player avatars.

```
career_athlete
  id, user_id→app_user, name, country_id→country, gender, created_at
  appearance(jsonb)          modular character config
  career_stage               rookie|regional|national|elite|world_class|champion|legend
  total_xp, level
  (uq user_id)               one career athlete per user for v1

career_attribute
  career_athlete_id, key, value       reaction|acceleration|max_speed|
                                      stride_efficiency|stamina|recovery|technique

career_equipment
  career_athlete_id, slot, item_id, equipped

game_result                  a completed 3D event
  id, career_athlete_id, event_definition_id, value_kind, value_num,
  value_text, reaction_ms, splits(jsonb), wind, is_valid, rejection_reason,
  rng_seed, input_digest, created_at
```

`game_result` is deliberately separate from the existing `Result` (which belongs
to a real-world `Participation`). Same *shape* — `value_kind` + comparable
magnitude — so leaderboard and record code can be shared, different ownership.

## 3. New: result validation (the security-critical piece)

Today `/api/v1/games/{code}/sessions` accepts a score with only per-engine
bounds. That is adequate for a tap game; it is not adequate for a competitive
3D leaderboard. §23 of the brief and the platform's own "never trust the client"
rule both demand more.

```
POST /api/v1/career/results
  { event, time_ms, reaction_ms, splits[], wind, rng_seed, input_digest }
```

Server-side checks, cheapest first:

1. **Hard plausibility** — reaction ≥ 100 ms (below is a false start by rule);
   time within the physical envelope for the event.
2. **Attribute ceiling** — the athlete's attributes imply a best possible time;
   a result meaningfully beyond it is rejected.
3. **Split coherence** — splits must sum to the total and follow a physically
   possible speed curve. Catches naive time-editing.
4. **Rate** — results per athlete per minute (reuses the existing limiter shape).
5. **Deferred re-simulation** — for leaderboard-topping results only, replay
   `(seed, input_digest, attributes)` server-side and compare. Expensive, so
   reserve it for records rather than every race.

Rejected results are **stored with `is_valid=false` and a reason**, not
discarded — silent rejection is indistinguishable from a bug, and the audit
trail is what makes cheat patterns visible.

## 4. New: leaderboards per event

The existing leaderboard is per *game code*. Extend to
`(event_definition, scope, period)`:

```
GET /api/v1/career/leaderboard?event=100m&scope=global|country|friends
                              &period=weekly|monthly|all_time
```

Reuse verbatim from the current implementation: one row per player, direction
awareness (`IsBetter`), country scoping. **Only validated results are eligible.**

## 5. New: cloud save with conflict resolution

```
GET  /api/v1/career/save        → { payload, version, updated_at }
PUT  /api/v1/career/save        → optimistic concurrency on version
```

On conflict, **do not silently pick a winner.** Merge additively where the data
allows (XP and unlocks are monotonic — take the max/union) and surface a genuine
divergence to the player. §30 says never lose progression; a last-write-wins
policy loses it quietly, which is the worst failure mode.

## 6. New: tournaments, social graph, analytics

- **Tournament** — bracket/round progression per edition; server owns
  advancement so a client cannot promote itself.
- **Friendship** — the missing piece behind the honestly-empty friends
  leaderboard. Mutual accept, so it can also gate ghost sharing.
- **Analytics** — batched event ingestion for the §32 event list. No unnecessary
  personal data.

## 7. Existing gaps to close (already known)

Carried from `PROJECT_PLAN.md`, unchanged by this pivot:

- Rate limiter is in-process; move to Redis before multi-replica deploy.
- `NotificationTransport` has no implementation (push not wired).
- Notification generation has no production caller.
- No real sports-data ingestion — matters only if the companion app ships.

## 8. Migration sequencing

| Step | Work | Blocked on Unreal? |
|---|---|---|
| 1 | `career_athlete` + attributes + migration | **No** |
| 2 | `game_result` + validation service + tests | **No** |
| 3 | Per-event leaderboards | **No** |
| 4 | Cloud save + conflict resolution | **No** |
| 5 | Tournament progression | **No** |
| 6 | Friendship graph | **No** |
| 7 | Analytics ingestion | **No** |

**None of the backend work requires Unreal to be installed.** It can proceed in
parallel with tooling setup, and each step is independently testable with pytest
against the existing suite. This is the highest-value work available right now.
