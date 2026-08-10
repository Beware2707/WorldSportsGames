from typing import Annotated

from fastapi import APIRouter, HTTPException, Query, Request, status

from app.ai.base import AIInsight, InsightKind
from app.api.deps import DbSession
from app.repositories.athlete import get_athlete_by_slug
from app.repositories.event import get_event_detail
from app.schemas.ai import AIInsightOut
from app.services.ai import (
    athlete_context,
    event_context,
    generate,
    head_to_head_context,
    trend_context,
)

router = APIRouter(prefix="/ai", tags=["ai"])


def _out(insight: AIInsight) -> AIInsightOut:
    return AIInsightOut(
        kind=insight.kind.value,
        text=insight.text,
        provider=insight.provider,
        is_estimate=insight.is_estimate,
        disclaimer=insight.disclaimer,
        basis=insight.basis,
        confidence=insight.confidence,
    )


@router.get("/athletes/{slug}/summary", response_model=AIInsightOut)
async def athlete_summary(slug: str, request: Request, session: DbSession) -> AIInsightOut:
    """Factual summary generated from recorded results."""
    athlete = await get_athlete_by_slug(session, slug)
    if athlete is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Athlete not found")
    context = await athlete_context(session, athlete)
    insight = await generate(
        request.app.state.ai_provider, InsightKind.ATHLETE_SUMMARY, context
    )
    return _out(insight)


@router.get("/athletes/{slug}/trend", response_model=AIInsightOut)
async def athlete_trend(slug: str, request: Request, session: DbSession) -> AIInsightOut:
    """Trend estimate. Always labelled — a trend is not a forecast."""
    athlete = await get_athlete_by_slug(session, slug)
    if athlete is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Athlete not found")
    context = await trend_context(session, athlete)
    insight = await generate(
        request.app.state.ai_provider, InsightKind.PERFORMANCE_TREND, context
    )
    return _out(insight)


@router.get("/head-to-head", response_model=AIInsightOut)
async def head_to_head(
    request: Request,
    session: DbSession,
    a: Annotated[str, Query(min_length=1)],
    b: Annotated[str, Query(min_length=1)],
) -> AIInsightOut:
    first = await get_athlete_by_slug(session, a)
    second = await get_athlete_by_slug(session, b)
    if first is None or second is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Athlete not found")
    if first.id == second.id:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="Pick two different athletes",
        )
    context = await head_to_head_context(session, first, second)
    insight = await generate(
        request.app.state.ai_provider, InsightKind.HEAD_TO_HEAD, context
    )
    return _out(insight)


@router.get("/events/{event_id}/preview", response_model=AIInsightOut)
async def event_preview(
    event_id: int, request: Request, session: DbSession
) -> AIInsightOut:
    event = await get_event_detail(session, event_id)
    if event is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Event not found")
    context = await event_context(session, event)
    insight = await generate(
        request.app.state.ai_provider, InsightKind.EVENT_PREVIEW, context
    )
    return _out(insight)


@router.get("/explain/result", response_model=AIInsightOut)
async def explain_result(
    request: Request,
    result_status: Annotated[str | None, Query(alias="status")] = None,
    position: Annotated[int | None, Query()] = None,
    value_text: Annotated[str | None, Query()] = None,
    value_kind: Annotated[str | None, Query()] = None,
) -> AIInsightOut:
    """Plain-language explanation of a result line (DNS/DNF/DSQ and finishes)."""
    insight = await generate(
        request.app.state.ai_provider,
        InsightKind.RESULT_EXPLANATION,
        {
            "status": result_status,
            "position": position,
            "value_text": value_text,
            "value_kind": value_kind or "result",
        },
    )
    return _out(insight)
