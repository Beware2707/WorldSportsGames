"""AI insight orchestration.

Gathers context from the database, hands it to the configured provider, and
falls back to the deterministic provider when an optional LLM is unavailable
or fails. Quality may degrade; correctness and labelling never do.
"""

import logging
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.ai.base import AIInsight, AIProvider, InsightKind
from app.ai.deterministic import DeterministicProvider
from app.models import Athlete, Event, Participation, Ranking, Result
from app.repositories.competitive import (
    athlete_medals,
    athlete_rankings,
    athlete_recent_results,
    athlete_records,
)

logger = logging.getLogger(__name__)

_FALLBACK = DeterministicProvider()

# Value kinds where a smaller number is a better performance. Everything else
# (points, distance, height, goals…) improves upward. Assuming only "time"
# counts down would invert golf strokes and equestrian penalties.
_LOWER_IS_BETTER_KINDS = frozenset({"time", "penalties", "strokes"})


def improves_downward(value_kind: str | None) -> bool:
    return value_kind in _LOWER_IS_BETTER_KINDS


async def generate(
    provider: AIProvider, kind: InsightKind, context: dict[str, Any]
) -> AIInsight:
    """Run the configured provider, falling back on any failure."""
    if provider is _FALLBACK:
        return await _FALLBACK.generate(kind, context)
    try:
        return await provider.generate(kind, context)
    except Exception:
        logger.warning(
            "AI provider %s failed for %s; using deterministic fallback",
            getattr(provider, "name", "?"),
            kind.value,
        )
        return await _FALLBACK.generate(kind, context)


async def athlete_context(session: AsyncSession, athlete: Athlete) -> dict:
    medals = await athlete_medals(session, athlete.id)
    records = await athlete_records(session, athlete.id)
    results = await athlete_recent_results(session, athlete.id)
    rankings = await athlete_rankings(session, athlete.id)

    return {
        "name": athlete.full_name,
        "country": athlete.country.name if athlete.country else None,
        "disciplines": [d.name for d in athlete.disciplines],
        "medals": [m.metal for m in medals],
        "records": [f"{r.kind} in {r.event_name}" for r in records],
        # Only finishes count: a DSQ or DNF is not a podium.
        "results": [
            result.position
            for result, _ in results
            if result.status == "ok" and result.position is not None
        ],
        "rankings": [
            {"discipline": r.discipline.name if r.discipline else None, "rank": r.rank}
            for r in rankings
        ],
    }


async def trend_context(session: AsyncSession, athlete: Athlete) -> dict:
    """Numeric series for trend analysis.

    Only comparable results are used: mixing a 100m time with a marathon time
    would produce a confident-sounding trend that means nothing. Values are
    grouped by discipline and the largest comparable group wins.
    """
    rows = await athlete_recent_results(session, athlete.id, limit=25)
    # Group by (discipline, value_kind, event): a 100m time and a long-jump
    # distance in the same discipline are not comparable, and neither are a
    # 100m time and a 200m time. Grouping by discipline alone averaged them
    # together — the exact mistake this function claims to prevent.
    grouped: dict[tuple[str, str, str], list[float]] = {}
    for result, event in rows:
        if result.value_num is None or result.status != "ok":
            continue
        key = (
            event.discipline.name if event.discipline else "unknown",
            result.value_kind,
            event.name,
        )
        grouped.setdefault(key, []).append(result.value_num)

    if not grouped:
        return {"values": [], "lower_is_better": True, "discipline": None}

    discipline, value_kind, event_name = max(grouped, key=lambda k: len(grouped[k]))
    values = list(reversed(grouped[(discipline, value_kind, event_name)]))
    return {
        "values": values,
        "lower_is_better": improves_downward(value_kind),
        "discipline": discipline,
        "event": event_name,
        "value_kind": value_kind,
        "name": athlete.full_name,
    }


async def head_to_head_context(
    session: AsyncSession, first: Athlete, second: Athlete
) -> dict:
    """Meetings are events both athletes actually contested."""
    rows = await session.execute(
        select(Participation, Result, Event)
        .join(Result, Result.participation_id == Participation.id, isouter=True)
        .join(Event, Participation.event_id == Event.id)
        .where(Participation.athlete_id.in_([first.id, second.id]))
        .options(selectinload(Event.discipline))
    )

    by_event: dict[int, dict[int, Any]] = {}
    event_names: dict[int, str] = {}
    for participation, result, event in rows.all():
        by_event.setdefault(event.id, {})[participation.athlete_id] = result
        event_names[event.id] = event.name

    meetings = []
    for event_id, entries in by_event.items():
        if first.id not in entries or second.id not in entries:
            continue
        a_result, b_result = entries[first.id], entries[second.id]
        # Both being *entered* in a future heat is not a meeting. A meeting is
        # an event they have actually contested, i.e. both have a result.
        if a_result is None or b_result is None:
            continue
        winner = None
        if a_result.position and b_result.position:
            if a_result.position < b_result.position:
                winner = "a"
            elif b_result.position < a_result.position:
                winner = "b"
            # Equal positions are a genuine tie — crediting B (as a bare
            # `else` did) would invent a result neither athlete achieved.
        meetings.append({"event": event_names[event_id], "winner": winner})

    return {
        "a": {"name": first.full_name},
        "b": {"name": second.full_name},
        "meetings": meetings,
    }


async def event_context(session: AsyncSession, event: Event) -> dict:
    """Entrants with their current ranking, if any.

    Ranks are fetched for all entrants in ONE query. Querying per entrant
    made a public, unauthenticated endpoint issue N round-trips for a field
    that is entirely optional.
    """
    athlete_ids = [p.athlete_id for p in event.participations]
    ranks: dict[int, int] = {}
    if athlete_ids and event.discipline_id:
        rows = await session.execute(
            select(Ranking.entity_id, Ranking.rank, Ranking.methodology)
            .where(
                Ranking.scope == "athlete",
                Ranking.entity_id.in_(athlete_ids),
                Ranking.discipline_id == event.discipline_id,
                # One methodology only: mixing ladders would rank an Elo 3rd
                # against a world-ranking 2nd as if they were comparable.
                Ranking.methodology == "world_ranking",
            )
            .order_by(Ranking.as_of.desc())
        )
        for entity_id, rank, _ in rows.all():
            ranks.setdefault(entity_id, rank)

    entrants = [
        {"name": p.athlete.full_name, "rank": ranks.get(p.athlete_id)}
        for p in event.participations
    ]
    return {
        "event_name": event.name,
        "discipline": event.discipline.name if event.discipline else None,
        "status": event.status,
        "entrants": entrants,
        "ranking_methodology": "world_ranking",
    }
