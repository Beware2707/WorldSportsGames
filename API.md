# API Design

FastAPI, versioned under `/api/v1`. OpenAPI at `/docs` (Swagger) and `/openapi.json`.
JSON only; UTC ISO-8601 timestamps. Errors follow one envelope:

```json
{ "detail": "human message", "code": "machine_code" }
```

## Conventions

- **Pagination**: `?page=1&size=20` → `{ "items": [...], "total": n, "page": p,
  "size": s, "pages": k }`. `size` capped at 100.
- **Filtering**: documented query params per resource (e.g. `?sport=athletics`,
  `?country=KEN`, `?category=summer`).
- **Sorting**: `?sort=name` / `?sort=-year` (leading `-` = descending), whitelisted
  fields per resource.
- **Auth**: `Authorization: Bearer <JWT>`. Public reads; writes and `/users/me*`
  require auth.
- **Caching** (Sprint 2): `Cache-Control` on catalogue reads, Redis-backed ETags.

## Sprint 1 endpoints (implemented)

```
GET  /health                          liveness + db connectivity

POST /api/v1/auth/register            {email, password, display_name} → user
POST /api/v1/auth/login               OAuth2 form → {access_token}
GET  /api/v1/auth/me                  current user (auth)

GET  /api/v1/sports                   ?category=&sort=  paginated
GET  /api/v1/sports/{code}            sport + its disciplines
GET  /api/v1/disciplines              ?sport=  paginated

GET  /api/v1/countries                ?sort=  paginated
GET  /api/v1/athletes                 ?sport=&discipline=&country=&q=&sort=
GET  /api/v1/athletes/{slug}          profile + disciplines + country
GET  /api/v1/competitions             ?level=&sport=&sort=
GET  /api/v1/competitions/{slug}      competition + editions

GET  /api/v1/home                     data-driven section feed (see below)
GET  /api/v1/search?q=                categorized results (sports/athletes/
                                      countries/competitions), min 2 chars

GET  /api/v1/events                   ?edition_id=&discipline=&status=  paginated
GET  /api/v1/events/{id}              detail + normalized results (ranked first)
GET  /api/v1/live                     events with genuine live coverage (often [])
WS   /api/v1/live/ws                  snapshot frame, then diff frames
POST /api/v1/live/dev/simulate        DEV ONLY (404 unless dev fixtures enabled)
```

### Live WS protocol

On connect the server sends `{"type": "snapshot", "events": [LiveEventOut…]}`,
then `{"type": "update", "event_id", "seq", "kind", "payload"}` frames where
`kind` ∈ `status | progress | results | note`. Simulated dev frames always
carry `payload.source = "dev-sim"`. Sports/countries list responses are served
through a fail-open Redis cache (5-minute TTL).

### Home feed contract

`GET /api/v1/home` returns an ordered list of sections the client renders
generically — the server decides composition, enabling personalization later
without app releases:

```json
{ "sections": [
    {"kind": "live_now",       "title": "Live Now",       "items": []},
    {"kind": "up_next",        "title": "Up Next",        "items": [...]},
    {"kind": "featured_competitions", "title": "Featured", "items": [...]},
    {"kind": "trending_athletes",     "title": "Trending", "items": [...]}
] }
```

`live_now` is empty until real live ingestion exists (Sprint 2) — the client shows
an honest empty state, never simulated live data.

## Sprint 2+ (specified)

```
/api/v1/live, /api/v1/events, /api/v1/results, /api/v1/rankings, /api/v1/records,
/api/v1/teams, /api/v1/news, /api/v1/videos, /api/v1/games, /api/v1/ai,
/api/v1/notifications, /api/v1/users/{id}/favorites (follow/unfollow)
WS   /api/v1/live/ws                  diff-based live updates (Redis fan-out)
```

Versioning policy: breaking changes → `/api/v2`; additive changes in place.
