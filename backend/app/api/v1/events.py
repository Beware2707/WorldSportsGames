from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, status

from app.api.deps import DbSession
from app.core.pagination import Page, PageParams, page_params, paginate
from app.repositories.event import events_query, get_event_detail, sorted_participations
from app.schemas.athlete import AthleteOut
from app.schemas.competition import EditionOut
from app.schemas.event import EventDetailOut, EventOut, EventResultOut

router = APIRouter(tags=["events"])


@router.get("/events", response_model=Page[EventOut])
async def list_events(
    session: DbSession,
    params: Annotated[PageParams, Depends(page_params)],
    edition_id: Annotated[int | None, Query()] = None,
    discipline: Annotated[str | None, Query()] = None,
    status_filter: Annotated[
        str | None,
        Query(alias="status", pattern="^(scheduled|live|completed|cancelled)$"),
    ] = None,
) -> Page[EventOut]:
    query = events_query(edition_id=edition_id, discipline=discipline, status=status_filter)
    items, total, pages = await paginate(session, query, params)
    return Page(
        items=[EventOut.model_validate(e) for e in items],
        total=total,
        page=params.page,
        size=params.size,
        pages=pages,
    )


@router.get("/events/{event_id}", response_model=EventDetailOut)
async def get_event(event_id: int, session: DbSession) -> EventDetailOut:
    event = await get_event_detail(session, event_id)
    if event is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Event not found")
    results = [
        EventResultOut(
            athlete=AthleteOut.model_validate(p.athlete),
            bib=p.bib,
            lane=p.lane,
            position=p.result.position if p.result else None,
            status=p.result.status if p.result else None,
            value_kind=p.result.value_kind if p.result else None,
            value_text=p.result.value_text if p.result else None,
            qualified=p.result.qualified if p.result else None,
        )
        for p in sorted_participations(event)
    ]
    return EventDetailOut(
        id=event.id,
        name=event.name,
        gender=event.gender,
        phase=event.phase,
        status=event.status,
        scheduled_start=event.scheduled_start,
        discipline=event.discipline,
        edition=EditionOut.model_validate(event.edition),
        competition_name=event.edition.competition.name,
        competition_slug=event.edition.competition.slug,
        results=results,
    )
