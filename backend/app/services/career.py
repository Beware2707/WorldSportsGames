"""Career events, result validation and progression.

This is the server side of "never trust the client" for the 3D game. A result
is stored no matter what, but it only counts — XP, personal bests,
leaderboards — if it survives validation. Checks run cheapest-first and the
first failure wins.

The event catalogue here is the server-side mirror of the Unreal
``UEventDefinition`` data assets. Keep the two in sync: an event the server
does not know is an event whose results cannot be validated.
"""

from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import CareerAthlete, GameResult
from app.models.career import ATTRIBUTE_KEYS, CAREER_STAGES

# ---- event catalogue ------------------------------------------------------


@dataclass(frozen=True)
class CareerEvent:
    code: str
    name: str
    value_kind: str  # time | distance | …
    lower_is_better: bool
    # Hard physical envelope. Outside this range no human result exists.
    min_plausible: float
    max_plausible: float
    # Attributes that set this event's performance ceiling.
    governing_attributes: tuple[str, ...]
    # Ceiling model: best achievable value at attr=0 and at attr=100.
    ceiling_at_zero: float
    ceiling_at_hundred: float
    splits_expected: int = 0
    requires_reaction: bool = False
    unit: str = "s"
    # No physically possible 10m segment is faster than this (seconds).
    min_split_seconds: float = 0.75


# Sub-100 ms reaction is a false start under World Athletics rules — and it is
# also the cheapest cheat to attempt.
FALSE_START_MS = 100.0

EVENTS: dict[str, CareerEvent] = {
    "sprint-100m": CareerEvent(
        code="sprint-100m",
        name="100m Sprint",
        value_kind="time",
        lower_is_better=True,
        min_plausible=9.0,
        max_plausible=60.0,
        governing_attributes=("reaction", "acceleration", "max_speed",
                              "stride_efficiency", "stamina"),
        # A brand-new athlete can at best run ~13.5; a maxed one ~9.55. The
        # skill margin below the ceiling is where play happens.
        ceiling_at_zero=13.5,
        ceiling_at_hundred=9.55,
        splits_expected=10,
        requires_reaction=True,
    ),
}

# Result submissions per athlete per minute. Nobody legitimately finishes
# more sprints than this; a script does.
MAX_RESULTS_PER_MINUTE = 10

# XP mirrors the 2D games' philosophy: every finish earns a floor, a personal
# best earns more, and the server computes all of it.
XP_PARTICIPATION = 15
XP_FIRST_RESULT = 20
XP_PERSONAL_BEST = 35

# Stage thresholds by total XP. Stages gate competition access, not power.
STAGE_THRESHOLDS: list[tuple[int, str]] = [
    (0, "rookie"),
    (500, "regional"),
    (1500, "national"),
    (4000, "elite"),
    (9000, "world_class"),
    (18000, "champion"),
    (32000, "legend"),
]


def stage_for_xp(total_xp: int) -> str:
    stage = CAREER_STAGES[0]
    for threshold, name in STAGE_THRESHOLDS:
        if total_xp >= threshold:
            stage = name
    return stage


# ---- validation -----------------------------------------------------------


@dataclass
class Submission:
    event: CareerEvent
    value_num: float
    reaction_ms: float | None = None
    splits: list[float] | None = None
    wind: float | None = None
    rng_seed: str | None = None
    input_digest: str | None = None
    client_ref: str | None = None
    errors: list[str] = field(default_factory=list)


def attribute_ceiling(event: CareerEvent, attributes: dict[str, float]) -> float:
    """Best value this athlete's attributes permit.

    Linear between the event's zero- and hundred-point ceilings on the mean of
    the governing attributes. Coarse by design: it exists to reject the
    impossible, not to model sprinting.
    """
    relevant = [attributes.get(key, 0.0) for key in event.governing_attributes]
    mean_attr = sum(relevant) / len(relevant) if relevant else 0.0
    fraction = max(0.0, min(mean_attr, 100.0)) / 100.0
    return event.ceiling_at_zero + (event.ceiling_at_hundred - event.ceiling_at_zero) * fraction


def validate_submission(
    submission: Submission, attributes: dict[str, float]
) -> str | None:
    """First failed check, or None if the result stands. Cheapest first."""
    event = submission.event
    value = submission.value_num

    # 1. Hard physical envelope.
    if not (event.min_plausible <= value <= event.max_plausible):
        return (
            f"value {value} outside the plausible range "
            f"[{event.min_plausible}, {event.max_plausible}]"
        )

    # 2. Reaction rules.
    if event.requires_reaction:
        if submission.reaction_ms is None:
            return "reaction time is required for this event"
        if submission.reaction_ms < FALSE_START_MS:
            return (
                f"reaction {submission.reaction_ms:.0f}ms is a false start "
                f"(under {FALSE_START_MS:.0f}ms)"
            )
        if submission.reaction_ms > 2000:
            return "reaction time implausibly slow"

    # 3. Split coherence.
    if submission.splits is not None and event.splits_expected:
        splits = submission.splits
        if len(splits) != event.splits_expected:
            return (
                f"expected {event.splits_expected} splits, got {len(splits)}"
            )
        if any(s <= 0 for s in splits):
            return "splits must be positive"
        if any(s < event.min_split_seconds for s in splits):
            return "a split implies impossible speed"
        total = sum(splits)
        # Splits measure running time; reaction is included in the official
        # time, so allow that plus a small tolerance.
        reaction_s = (submission.reaction_ms or 0.0) / 1000.0
        if abs(total + reaction_s - value) > max(0.05, value * 0.01):
            return (
                f"splits ({total:.3f}s) plus reaction do not sum to the "
                f"recorded time ({value:.3f}s)"
            )

    # 4. Wind legality. A tailwind over +2.0 m/s is wind-assisted and cannot
    #    stand as a record-eligible mark under World Athletics rules — so a
    #    time set with a huge tailwind must not top a leaderboard.
    if submission.wind is not None and submission.wind > 2.0:
        return f"wind {submission.wind:+.1f} m/s exceeds the +2.0 legal limit"

    # 5. Attribute ceiling.
    ceiling = attribute_ceiling(event, attributes)
    if event.lower_is_better:
        if value < ceiling - 0.01:
            return (
                f"result {value:.3f} is beyond this athlete's attribute "
                f"ceiling ({ceiling:.3f})"
            )
    elif value > ceiling + 0.01:
        return (
            f"result {value:.3f} is beyond this athlete's attribute "
            f"ceiling ({ceiling:.3f})"
        )

    return None


# ---- persistence ----------------------------------------------------------


def format_value(event: CareerEvent, value: float) -> str:
    if event.value_kind == "time":
        if value < 60:
            return f"{value:.2f}"
        minutes, seconds = divmod(value, 60)
        return f"{int(minutes)}:{seconds:05.2f}"
    return f"{value:g}"


async def get_athlete_for_user(
    session: AsyncSession, user_id: int
) -> CareerAthlete | None:
    from sqlalchemy.orm import selectinload

    return (
        await session.execute(
            select(CareerAthlete)
            .where(CareerAthlete.user_id == user_id)
            .options(
                selectinload(CareerAthlete.attributes),
                selectinload(CareerAthlete.country),
            )
        )
    ).scalar_one_or_none()


def attributes_dict(athlete: CareerAthlete) -> dict[str, float]:
    return {attribute.key: attribute.value for attribute in athlete.attributes}


async def recent_result_count(
    session: AsyncSession, athlete_id: int, window: timedelta
) -> int:
    since = datetime.now(UTC) - window
    return (
        await session.execute(
            select(func.count())
            .select_from(GameResult)
            .where(
                GameResult.career_athlete_id == athlete_id,
                GameResult.created_at >= since,
            )
        )
    ).scalar_one()


async def personal_best(
    session: AsyncSession, athlete_id: int, event: CareerEvent
) -> float | None:
    aggregate = func.min if event.lower_is_better else func.max
    return (
        await session.execute(
            select(aggregate(GameResult.value_num)).where(
                GameResult.career_athlete_id == athlete_id,
                GameResult.event_code == event.code,
                GameResult.is_valid.is_(True),
            )
        )
    ).scalar_one_or_none()


async def record_result(
    session: AsyncSession,
    athlete: CareerAthlete,
    submission: Submission,
) -> tuple[GameResult, bool]:
    """Validate, store (always), and apply progression (valid results only).

    Returns (row, is_personal_best). DB-based rate limiting is checked here so
    it holds across replicas — unlike the in-process HTTP limiter.
    """
    event = submission.event

    # Lock the athlete row so concurrent submissions for the same athlete
    # serialize: the count-then-insert rate check is otherwise a TOCTOU race
    # where a burst all read the same stale count and all insert. (No-op on
    # SQLite; enforced on Postgres, which is where a burst would land.) Also
    # serializes the total_xp increment below against lost updates.
    await session.execute(
        select(CareerAthlete.id)
        .where(CareerAthlete.id == athlete.id)
        .with_for_update()
    )

    reason = validate_submission(submission, attributes_dict(athlete))
    if reason is None:
        count = await recent_result_count(session, athlete.id, timedelta(minutes=1))
        if count >= MAX_RESULTS_PER_MINUTE:
            reason = "too many results in the last minute"

    previous = await personal_best(session, athlete.id, event)
    is_valid = reason is None
    improved = is_valid and (
        previous is None
        or (submission.value_num < previous if event.lower_is_better
            else submission.value_num > previous)
    )

    xp = 0
    if is_valid:
        xp = XP_PARTICIPATION
        if previous is None:
            xp += XP_FIRST_RESULT
        elif improved:
            xp += XP_PERSONAL_BEST

    row = GameResult(
        career_athlete_id=athlete.id,
        event_code=event.code,
        value_kind=event.value_kind,
        value_num=submission.value_num,
        value_text=format_value(event, submission.value_num),
        reaction_ms=submission.reaction_ms,
        splits=submission.splits,
        wind=submission.wind,
        rng_seed=submission.rng_seed,
        input_digest=submission.input_digest,
        client_ref=submission.client_ref,
        is_valid=is_valid,
        rejection_reason=reason,
        xp_awarded=xp,
        was_pb=improved,
    )
    session.add(row)

    if is_valid:
        athlete.total_xp += xp
        athlete.career_stage = stage_for_xp(athlete.total_xp)

    await session.flush()
    return row, improved


def default_attributes() -> dict[str, float]:
    return dict.fromkeys(ATTRIBUTE_KEYS, 40.0)


# ---- cloud save merge -----------------------------------------------------

# Keys that only ever grow. On conflict these merge automatically; anything
# else is a genuine divergence the client must resolve.
_MONOTONIC_KEYS = ("total_xp", "sessions_played")
_UNION_KEYS = ("unlocks", "achievements", "cosmetics")


def _numeric_max(a, b):
    """max() over values that may not be comparable numbers.

    The payload is arbitrary client JSON, so a monotonic key could arrive as a
    string. Only compare when both sides are real numbers (and not bools);
    otherwise fall back to the newer write rather than raising."""
    a_num = isinstance(a, int | float) and not isinstance(a, bool)
    b_num = isinstance(b, int | float) and not isinstance(b, bool)
    if a_num and b_num:
        return max(a, b)
    return b if b is not None else a


def _union_lists(a, b) -> list:
    """Union of two collections that may hold unhashable objects.

    Hashable items (ids as strings/ints) de-duplicate; object items (e.g.
    ``{"id": "x"}``) are compared by value since they cannot go in a set.
    Never raises on the client's chosen shape — a merge that 500s defeats the
    whole point of returning a merge suggestion."""
    a = a if isinstance(a, list) else []
    b = b if isinstance(b, list) else []
    merged: list = []
    for item in [*a, *b]:
        if item not in merged:
            merged.append(item)
    try:
        return sorted(merged)
    except TypeError:
        # Mixed or unorderable types — preserve union order rather than crash.
        return merged


def merge_saves(ours: dict, theirs: dict) -> dict:
    """Additive merge of two save payloads.

    Monotonic counters take the max; unlock collections take the union; any
    other key takes the newer write (``theirs``), which is safe only because
    the caller has already surfaced the conflict to the player.

    Total-function by construction: ``ours``/``theirs`` are arbitrary client
    JSON, and this runs while building a 409 body — an exception here would
    turn the conflict response into a 500 and hide the merge the client needs.
    """
    merged = {**ours, **theirs}
    for key in _MONOTONIC_KEYS:
        if key in ours or key in theirs:
            merged[key] = _numeric_max(ours.get(key), theirs.get(key))
    for key in _UNION_KEYS:
        if key in ours or key in theirs:
            merged[key] = _union_lists(ours.get(key), theirs.get(key))
    return merged
