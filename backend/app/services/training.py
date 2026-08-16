"""Training: the server-owned path from playing a drill to a better athlete.

Design rules this file exists to enforce:

1. **The server decides every increment.** The client reports what it did
   (a reaction time, a cadence score); it never reports a gain.
2. **Gains diminish toward the ceiling.** The same drill performance is worth
   far less at 90 than at 40, so grinding cannot substitute for playing well
   and no attribute ever reaches a value the design did not intend.
3. **Effort is bounded per day.** Without a cap, an autoclicker beats a
   player, which is the exact failure the sprint's cadence model was built to
   avoid (GAME_DESIGN.md §2).
4. **Attributes raise the ceiling; they do not win races** (§4/§45). Training
   must therefore stay slow relative to skill, and it is capped well short of
   letting attributes decide a race on their own.
"""

from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.career import CareerAthlete, CareerAttribute
from app.models.training import TrainingSession
from app.services.career import stage_for_xp


# A drill's metric is either "lower is better" (a reaction time in ms) or
# "higher is better" (a score out of 100). Best/worst bound the plausible
# envelope AND define the quality scale.
@dataclass(frozen=True)
class Drill:
    code: str
    name: str
    attribute: str
    metric_unit: str
    lower_is_better: bool
    # Values outside [min_metric, max_metric] are refused as implausible.
    min_metric: float
    max_metric: float
    # The metric that counts as a perfect (quality 1.0) performance, and the
    # one that counts as no better than showing up (quality 0.0).
    best_metric: float
    worst_metric: float


DRILLS: dict[str, Drill] = {
    "reaction-drill": Drill(
        code="reaction-drill",
        name="Reaction Drill",
        attribute="reaction",
        metric_unit="ms",
        lower_is_better=True,
        # Below the false-start floor is not a reaction to anything.
        min_metric=100.0,
        max_metric=2000.0,
        best_metric=120.0,
        worst_metric=400.0,
    ),
    "sled-push": Drill(
        code="sled-push",
        name="Sled Push",
        attribute="acceleration",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
    "flying-sprint": Drill(
        code="flying-sprint",
        name="Flying Sprint",
        attribute="max_speed",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
    "rhythm-drill": Drill(
        code="rhythm-drill",
        name="Rhythm Drill",
        attribute="stride_efficiency",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
    "tempo-run": Drill(
        code="tempo-run",
        name="Tempo Run",
        attribute="stamina",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
    "form-drill": Drill(
        code="form-drill",
        name="Form Drill",
        attribute="technique",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
    "recovery-session": Drill(
        code="recovery-session",
        name="Recovery Session",
        attribute="recovery",
        metric_unit="score",
        lower_is_better=False,
        min_metric=0.0,
        max_metric=100.0,
        best_metric=95.0,
        worst_metric=25.0,
    ),
}

ATTRIBUTE_CEILING = 99.0
# A perfect session at attribute 0 is worth this much; the gain shrinks
# quadratically with how close the attribute already is to the ceiling.
MAX_GAIN_AT_ZERO = 1.60
# Total attribute points any single attribute may gain in a rolling day.
DAILY_ATTRIBUTE_CAP = 4.0
# Sessions counted per athlete per rolling day, accepted or not.
MAX_SESSIONS_PER_DAY = 40
XP_PER_SESSION = 8
XP_QUALITY_BONUS = 22


def quality_for(drill: Drill, metric: float) -> float:
    """0..1 how good this performance was, clamped to the drill's scale."""
    span = drill.worst_metric - drill.best_metric
    if span == 0:
        return 0.0
    raw = (drill.worst_metric - metric) / span
    if not drill.lower_is_better:
        raw = (metric - drill.worst_metric) / (drill.best_metric - drill.worst_metric)
    return max(0.0, min(1.0, raw))


def gain_for(quality: float, current: float) -> float:
    """Attribute points earned, diminishing as the attribute rises.

    Quadratic falloff: at 40 a perfect session is worth ~0.58 points, at 90
    ~0.02. Improving a strong athlete is meant to be slow — that is what
    keeps execution, not accumulation, the thing that wins races.
    """
    headroom = max(0.0, ATTRIBUTE_CEILING - current) / ATTRIBUTE_CEILING
    return MAX_GAIN_AT_ZERO * quality * headroom * headroom


@dataclass
class TrainingSubmission:
    drill: Drill
    metric: float
    client_ref: str | None = None


async def sessions_in_last_day(session: AsyncSession, athlete_id: int) -> int:
    since = datetime.now(UTC) - timedelta(days=1)
    return (
        await session.execute(
            select(func.count())
            .select_from(TrainingSession)
            .where(
                TrainingSession.career_athlete_id == athlete_id,
                TrainingSession.created_at >= since,
            )
        )
    ).scalar_one()


async def gained_in_last_day(
    session: AsyncSession, athlete_id: int, attribute_key: str
) -> float:
    since = datetime.now(UTC) - timedelta(days=1)
    total = (
        await session.execute(
            select(func.coalesce(func.sum(TrainingSession.attribute_gain), 0.0)).where(
                TrainingSession.career_athlete_id == athlete_id,
                TrainingSession.attribute_key == attribute_key,
                TrainingSession.created_at >= since,
            )
        )
    ).scalar_one()
    return float(total)


def validate(drill: Drill, metric: float) -> str | None:
    """First failed check, or None. The client's number must be possible."""
    if metric != metric or metric in (float("inf"), float("-inf")):
        return "metric must be a finite number"
    if not (drill.min_metric <= metric <= drill.max_metric):
        return (
            f"{drill.metric_unit} {metric} outside the plausible range "
            f"[{drill.min_metric}, {drill.max_metric}]"
        )
    return None


async def record_session(
    session: AsyncSession,
    athlete: CareerAthlete,
    submission: TrainingSubmission,
) -> TrainingSession:
    """Validate, store (always), and apply the gain (accepted sessions only)."""
    drill = submission.drill

    # Serialize concurrent sessions for this athlete: the count-then-insert
    # cap is otherwise a TOCTOU race a burst walks straight through.
    await session.execute(
        select(CareerAthlete.id)
        .where(CareerAthlete.id == athlete.id)
        .with_for_update()
    )

    reason = validate(drill, submission.metric)
    if reason is None and await sessions_in_last_day(session, athlete.id) >= MAX_SESSIONS_PER_DAY:
        reason = "daily training limit reached — rest and come back"

    attribute = next(
        (a for a in athlete.attributes if a.key == drill.attribute), None
    )
    before = attribute.value if attribute else 0.0

    quality = 0.0
    gain = 0.0
    xp = 0
    if reason is None:
        quality = quality_for(drill, submission.metric)
        gain = gain_for(quality, before)

        # The daily cap trims the gain rather than refusing the session: a
        # player who trains all day still gets their last session recorded,
        # it is simply worth nothing more.
        already = await gained_in_last_day(session, athlete.id, drill.attribute)
        remaining = max(0.0, DAILY_ATTRIBUTE_CAP - already)
        gain = min(gain, remaining)
        gain = min(gain, max(0.0, ATTRIBUTE_CEILING - before))
        xp = XP_PER_SESSION + int(round(XP_QUALITY_BONUS * quality))

    row = TrainingSession(
        career_athlete_id=athlete.id,
        drill=drill.code,
        attribute_key=drill.attribute,
        metric=submission.metric,
        quality=quality,
        attribute_before=before,
        attribute_gain=gain,
        xp_awarded=xp,
        accepted=reason is None,
        rejection_reason=reason,
        client_ref=submission.client_ref,
    )
    session.add(row)

    if reason is None:
        if attribute is None:
            attribute = CareerAttribute(
                career_athlete_id=athlete.id, key=drill.attribute, value=gain
            )
            session.add(attribute)
        else:
            attribute.value = min(ATTRIBUTE_CEILING, before + gain)
        athlete.total_xp += xp
        athlete.career_stage = stage_for_xp(athlete.total_xp)

    return row
