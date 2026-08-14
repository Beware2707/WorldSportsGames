import json

from fastapi import APIRouter

from app.api.deps import DbSession, OptionalUser
from app.models import AnalyticsEvent
from app.models.social import ANALYTICS_EVENT_NAMES
from app.schemas.social import AnalyticsBatchIn, AnalyticsBatchOut, AnalyticsRejection

router = APIRouter(prefix="/analytics", tags=["analytics"])

# A single event's properties must stay small — analytics is a signal, not a
# blob store, and an unbounded JSON column is a slow-motion disk leak.
_MAX_PROPERTIES_BYTES = 4096


@router.post("/events", response_model=AnalyticsBatchOut, status_code=202)
async def ingest(
    body: AnalyticsBatchIn, session: DbSession, user: OptionalUser
) -> AnalyticsBatchOut:
    """Batched analytics ingestion.

    Anonymous events are allowed (SessionStarted fires before login). Unknown
    event names are rejected per-event rather than failing the batch, so one
    bad client build cannot lose a whole session's telemetry.
    """
    rejected: list[AnalyticsRejection] = []
    accepted = 0

    for index, event in enumerate(body.events):
        if event.name not in ANALYTICS_EVENT_NAMES:
            rejected.append(
                AnalyticsRejection(index=index, reason=f"unknown event {event.name!r}")
            )
            continue
        if len(json.dumps(event.properties, default=str)) > _MAX_PROPERTIES_BYTES:
            rejected.append(
                AnalyticsRejection(index=index, reason="properties too large")
            )
            continue
        session.add(
            AnalyticsEvent(
                user_id=user.id if user else None,
                name=event.name,
                properties=event.properties,
                client_ts=event.client_ts,
            )
        )
        accepted += 1

    await session.commit()
    return AnalyticsBatchOut(accepted=accepted, rejected=rejected)
