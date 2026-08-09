"""Development-only live event simulator.

Drives a *seeded, fictional* scheduled event through a full live lifecycle
(live → progress updates → results → completed) so the Live Center can be
exercised locally before real provider ingestion (Sprint 6+) exists.

Strictly dev-gated: the endpoint exposing this returns 404 unless
``SPORTS_ENABLE_DEV_FIXTURES=true``. Every update payload carries
``"source": "dev-sim"`` so simulated data can never masquerade as a real feed.
"""

import asyncio

from sqlalchemy.ext.asyncio import AsyncSession

from app.models import Event, LiveEvent, Result
from app.repositories.event import get_result_for_participation
from app.repositories.live import append_update, get_live_for_event
from app.services.live import LiveHub

SOURCE = "dev-sim"


def _format_time(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.2f}"
    minutes, secs = divmod(seconds, 60)
    return f"{int(minutes)}:{secs:05.2f}"


async def simulate_event(
    session: AsyncSession,
    hub: LiveHub,
    event: Event,
    steps: int = 5,
    interval: float = 1.0,
) -> None:
    """Run the full simulated lifecycle for ``event``. Commits as it goes."""
    live = await get_live_for_event(session, event.id)
    if live is None:
        live = LiveEvent(event_id=event.id, status="live", current_phase=event.phase)
        session.add(live)
        await session.flush()
    live.status = "live"
    event.status = "live"
    update = await append_update(
        session, live.id, "status", {"status": "live", "source": SOURCE}
    )
    await session.commit()
    await hub.publish(_message(event.id, update.seq, "status", update.payload))

    for step in range(1, steps + 1):
        if interval > 0:
            await asyncio.sleep(interval)
        update = await append_update(
            session,
            live.id,
            "progress",
            {"step": step, "total": steps, "phase": event.phase, "source": SOURCE},
        )
        await session.commit()
        await hub.publish(_message(event.id, update.seq, "progress", update.payload))

    # Final standings: deterministic synthetic times per participant order.
    standings = []
    for index, participation in enumerate(event.participations):
        seconds = 10.62 + index * 0.11
        existing = await get_result_for_participation(session, participation.id)
        if existing is None:
            session.add(
                Result(
                    participation_id=participation.id,
                    position=index + 1,
                    status="ok",
                    value_kind="time",
                    value_num=seconds,
                    value_text=_format_time(seconds),
                )
            )
        standings.append({"position": index + 1, "value": _format_time(seconds)})
    update = await append_update(
        session, live.id, "results", {"standings": standings, "source": SOURCE}
    )
    live.status = "finished"
    event.status = "completed"
    await session.commit()
    await hub.publish(_message(event.id, update.seq, "results", update.payload))

    final = await append_update(
        session, live.id, "status", {"status": "completed", "source": SOURCE}
    )
    await session.commit()
    await hub.publish(_message(event.id, final.seq, "status", final.payload))


def _message(event_id: int, seq: int, kind: str, payload: dict) -> dict:
    return {
        "type": "update",
        "event_id": event_id,
        "seq": seq,
        "kind": kind,
        "payload": payload,
    }
