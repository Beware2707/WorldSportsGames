# Database Schema

PostgreSQL, SQLAlchemy 2.0 typed ORM, Alembic migrations. All timestamps UTC
(`timestamptz`). Integer surrogate PKs; natural keys get unique indexes. Sprint 1
tables are implemented; later tables are specified here so migrations only ever add.

## Sprint 1 (implemented)

```
sport             id, code(uq), name, category(summer|winter|la28), icon, sort_order
discipline        id, sport_id→sport, code, name, (uq sport_id+code)
country           id, iso3(uq), iso2, name, flag_emoji
athlete           id, slug(uq), given_name, family_name, country_id→country,
                  date_of_birth?, sex?, headshot_url?, bio?, is_active
athlete_discipline athlete_id→athlete, discipline_id→discipline  (PK both; an
                  athlete can compete in many disciplines)
competition       id, slug(uq), name, level(olympic|world|continental|league|
                  national|other), sport_id?→sport  (NULL = multi-sport, e.g. Games)
competition_edition id, competition_id→competition, label, year,
                  start_date?, end_date?, host_city?, host_country_id?→country,
                  status(upcoming|live|completed), (uq competition_id+label)
app_user          id, email(uq), hashed_password, display_name, is_active,
                  created_at
favorite          id, user_id→app_user, entity_type(sport|discipline|athlete|
                  country|competition), entity_id, created_at,
                  (uq user_id+entity_type+entity_id)
```

Indexes: every FK; `athlete(country_id)`, `competition_edition(status, start_date)`,
`favorite(user_id)`.

## Sprint 2+ (specified, not yet migrated)

```
venue             id, name, city, country_id
event             id, discipline_id, edition_id, venue_id?, name, gender_class,
                  phase(heat|semi|final|…), scheduled_start, status
participation     id, event_id, athlete_id?, team_id?, bib?, lane?
team              id, country_id, sport_id, name
result            id, participation_id, position?, status(ok|DNS|DNF|DSQ|tie),
                  value_kind(time|distance|height|score|points|goals|sets|rounds),
                  value_num?, value_text, qualified?
result_detail     id, result_id, key, value          (splits, set scores, rounds)
live_event        id, event_id, status, current_phase, updated_at
live_update       id, live_event_id, seq, kind, payload(jsonb), created_at
ranking           id, scope(athlete|team|country), discipline_id?, sport_id?,
                  methodology, entity_id, rank, points, as_of_date
record            id, kind(WR|OR|CR|NR|PB|SB…), event_ref, athlete_id?, team_id?,
                  country_id?, value_text, value_num?, unit, date, location,
                  edition_id?
medal             id, edition_id, event_id, athlete_id?, team_id?, country_id,
                  metal(gold|silver|bronze)
news / video      id, title, url, source, published_at + M2M link tables to
                  sport/athlete/country/competition
notification      id, user_id, kind, payload(jsonb), read_at?, created_at
notification_pref user_id, kind, enabled
game              id, code, name, engine_params(jsonb)
game_session      id, game_id, user_id, score, xp, created_at
achievement       id, code, name, criteria(jsonb); user_achievement M2M
leaderboard       materialized per game/scope(global|country|friends)/period
user_preference   user_id, key, value(jsonb)   (onboarding selections, feed config)
```

Design notes

- **Results are polymorphic by value_kind**, not by table — one normalized row,
  sport-specific renderers client-side. `result_detail` holds sport-specific
  breakdowns without schema churn.
- **`favorite` is entity-polymorphic** (type+id) to keep "follow anything" uniform.
- **No duplicated denormalized names** — joins or cached projections only.
- Historical scale: results/live_update are append-only; partition by edition/year
  when volume demands (documented decision, deferred).
