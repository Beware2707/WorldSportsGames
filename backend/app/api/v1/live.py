import asyncio
from typing import Annotated

from fastapi import (
    APIRouter,
    Depends,
    HTTPException,
    Query,
    Request,
    WebSocket,
    WebSocketDisconnect,
    status,
)

from app.api.deps import CurrentUser, DbSession, require_dev_mode
from app.repositories.event import get_event_detail, get_scheduled_event_with_participants
from app.repositories.live import get_live_events, last_seq_map
from app.schemas.event import EventOut
from app.schemas.live import LiveEventOut
from app.services.dev_sim import simulate_event

router = APIRouter(tags=["live"])


async def _live_snapshot(session) -> list[LiveEventOut]:
    coverage = await get_live_events(session)
    seqs = await last_seq_map(session, [c.id for c in coverage])  # one query, no N+1
    return [
        LiveEventOut(
            event=EventOut.model_validate(live.event),
            edition_label=live.event.edition.label,
            competition_name=live.event.edition.competition.name,
            competition_slug=live.event.edition.competition.slug,
            current_phase=live.current_phase,
            last_seq=seqs.get(live.id, 0),
            updated_at=live.updated_at,
        )
        for live in coverage
    ]


@router.get("/live", response_model=list[LiveEventOut])
async def live_events(session: DbSession) -> list[LiveEventOut]:
    """Events with genuine live coverage right now. Empty when nothing is live."""
    return await _live_snapshot(session)


@router.websocket("/live/ws")
async def live_ws(ws: WebSocket) -> None:
    """Live stream: snapshot frame, then diffs.

    The DB session is opened and closed around the snapshot only — a
    request-scoped dependency would pin a pooled connection for the entire
    (potentially hours-long) socket lifetime and exhaust the pool.
    """
    await ws.accept()
    hub = ws.app.state.live_hub

    # Register BEFORE snapshotting so updates published while the snapshot
    # query runs are delivered rather than lost in the gap. Clients dedupe by
    # seq, so an update that is also reflected in the snapshot is harmless.
    await hub.register(ws)
    try:
        async with ws.app.state.session_factory() as session:
            snapshot = await _live_snapshot(session)
        await ws.send_json(
            {"type": "snapshot", "events": [e.model_dump(mode="json") for e in snapshot]}
        )
        while True:
            # Inbound frames are keepalives only; the stream is server → client.
            await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        await hub.unregister(ws)


@router.post(
    "/live/dev/simulate",
    status_code=status.HTTP_202_ACCEPTED,
    dependencies=[Depends(require_dev_mode)],
)
async def dev_simulate(
    request: Request,
    session: DbSession,
    user: CurrentUser,
    event_id: Annotated[int | None, Query()] = None,
    steps: Annotated[int, Query(ge=0, le=60)] = 5,
    interval: Annotated[float, Query(ge=0, le=10)] = 1.0,
    wait: Annotated[bool, Query()] = False,
) -> dict:
    """DEV ONLY: drive a seeded fictional event through a live lifecycle.

    Double-gated by ``require_dev_mode`` (404 unless BOTH ``debug`` and
    ``enable_dev_fixtures`` are on) and additionally requires an authenticated
    user — a misconfigured deployment must not let anonymous callers fabricate
    live coverage. Every emitted payload is tagged ``source: dev-sim``.
    """
    if event_id is not None:
        event = await get_event_detail(session, event_id)
        if event is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Event not found"
            )
    else:
        event = await get_scheduled_event_with_participants(session)
        if event is None:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="No scheduled event with participants to simulate",
            )

    hub = request.app.state.live_hub
    if wait:
        await simulate_event(session, hub, event, steps=steps, interval=interval)
        return {"status": "completed", "event_id": event.id}

    session_factory = request.app.state.session_factory

    async def _run() -> None:
        async with session_factory() as background_session:
            background_event = await get_event_detail(background_session, event.id)
            if background_event is not None:
                await simulate_event(
                    background_session, hub, background_event, steps=steps, interval=interval
                )

    task = asyncio.create_task(_run())
    request.app.state.background_tasks.add(task)
    task.add_done_callback(request.app.state.background_tasks.discard)
    return {"status": "started", "event_id": event.id}
