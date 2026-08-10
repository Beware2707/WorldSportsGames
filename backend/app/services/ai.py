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
from app.models import Athlete, Event, Participation, Result
from app.repositories.competitive import (
    athlete_medals,
    athlete_rankings,
    athlete_recent_results,
    athlete_records,
)

logger = logging.getLogger(__name__)

_FALLBACK = DeterministicProvider()


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
        "results": [result.position for result, _ in results],
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
    by_discipline: dict[str, list[float]] = {}
    kinds: dict[str, str] = {}
    for result, event in rows:
        if result.value_num is None or result.status != "ok":
            continue
        key = event.discipline.name if event.discipline else "unknown"
        by_discipline.setdefault(key, []).append(result.value_num)
        kinds[key] = result.value_kind

    if not by_discipline:
        return {"values": [], "lower_is_better": True, "discipline": None}

    discipline = max(by_discipline, key=lambda k: len(by_discipline[k]))
    values = list(reversed(by_discipline[discipline]))  # oldest first
    return {
        "values": values,
        # Times and other duration-like measures improve downward; points,
        # distances and heights improve upward.
        "lower_is_better": kinds.get(discipline) == "time",
        "discipline": discipline,
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
            winner = "a" if a_result.position < b_result.position else "b"
        meetings.append({"event": event_names[event_id], "winner": winner})

    return {
        "a": {"name": first.full_name},
        "b": {"name": second.full_name},
        "meetings": meetings,
    }


async def event_context(session: AsyncSession, event: Event) -> dict:
    """Entrants with their current ranking, if any."""
    entrants = []
    for participation in event.participations:
        athlete = participation.athlete
        rankings = await athlete_rankings(session, athlete.id)
        rank = None
        for row in rankings:
            if event.discipline_id and row.discipline_id == event.discipline_id:
                rank = row.rank
                break
        entrants.append({"name": athlete.full_name, "rank": rank})

    return {
        "event_name": event.name,
        "discipline": event.discipline.name if event.discipline else None,
        "status": event.status,
        "entrants": entrants,
    }
