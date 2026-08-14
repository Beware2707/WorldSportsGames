"""Tournament progression.

Round structure per the brief (§5): Qualification → Heat → Semifinal → Final.
The AI field for each round is generated server-side, deterministically from
the tournament id, and stored — so the client can present the opponents but
can never choose them, and re-reading a round always shows the same field.
"""

import random

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.models import CareerAthlete, CareerTournament, Country, TournamentRound
from app.services.career import (
    EVENTS,
    CareerEvent,
    attribute_ceiling,
    attributes_dict,
    stage_for_xp,
)

ROUND_ORDER = ["qualification", "heat", "semifinal", "final"]

# (opponents in field, how many total placings advance). Final advances nobody;
# it produces the tournament's final position instead.
ROUND_RULES: dict[str, tuple[int, int | None]] = {
    "qualification": (8, 6),
    "heat": (7, 4),
    "semifinal": (7, 3),
    "final": (7, None),
}

# Fictional opponent names only — never real athletes.
_GIVEN = ["Asha", "Bolt-", "Caro", "Dema", "Enzo", "Fola", "Gio", "Hana",
          "Iker", "Jala", "Kofi", "Lena", "Mika", "Nia", "Owen", "Pia",
          "Quin", "Rio", "Sana", "Tomo"]
_FAMILY = ["Adeyemi", "Brandt", "Costa", "Diallo", "Eriksen", "Fujita",
           "Gerber", "Herrera", "Ivanov", "Jansen", "Karim", "Laurent",
           "Moreau", "Ndiaye", "Okada", "Petrov", "Quintero", "Rossi",
           "Sato", "Tanaka"]

# Medal XP on top of the run's own XP. Server-side only, like all XP.
MEDAL_XP = {1: 120, 2: 80, 3: 50}


def _field_seed(tournament_id: int, round_name: str) -> int:
    return tournament_id * 1009 + ROUND_ORDER.index(round_name)


def generate_field(
    tournament_id: int,
    round_name: str,
    event: CareerEvent,
    athlete_ceiling: float,
    country_iso3s: list[str],
) -> list[dict]:
    """Deterministic AI field for one round.

    Opponent times are spread around the athlete's own ceiling so the
    tournament is competitive at any attribute level, and the spread tightens
    each round so the final is genuinely hard.
    """
    rng = random.Random(_field_seed(tournament_id, round_name))
    opponents, _ = ROUND_RULES[round_name]
    round_index = ROUND_ORDER.index(round_name)

    # Qualification opponents are beatable on a decent run; finalists are not
    # far off the athlete's absolute best.
    slack_floor = [0.15, 0.08, 0.02, -0.02][round_index]
    slack_span = [1.4, 1.0, 0.7, 0.5][round_index]

    field = []
    for _ in range(opponents):
        offset = slack_floor + rng.random() * slack_span
        time = round(
            min(max(athlete_ceiling + offset, event.min_plausible),
                event.max_plausible),
            2,
        )
        field.append(
            {
                "name": f"{rng.choice(_GIVEN)} {rng.choice(_FAMILY)}".replace("- ", " "),
                "iso3": rng.choice(country_iso3s) if country_iso3s else None,
                "time": time,
            }
        )
    return field


def position_in_field(player_time: float, field: list[dict], event: CareerEvent) -> int:
    """1-based finishing position. Ties go to the player."""
    if event.lower_is_better:
        beaten_by = sum(1 for opponent in field if opponent["time"] < player_time)
    else:
        beaten_by = sum(1 for opponent in field if opponent["time"] > player_time)
    return beaten_by + 1


async def country_pool(session: AsyncSession) -> list[str]:
    rows = await session.execute(select(Country.iso3))
    return [row[0] for row in rows.all()]


async def get_tournament(
    session: AsyncSession,
    tournament_id: int,
    athlete_id: int,
    for_update: bool = False,
) -> CareerTournament | None:
    """Owner-scoped fetch: another athlete's tournament is invisible.

    ``for_update`` locks the tournament row so two concurrent round
    submissions serialize — the second sees the round already scored and is
    rejected rather than double-advancing. (No-op on SQLite; enforced on
    Postgres, backed up by the uq_tournament_round constraint either way.)
    """
    query = (
        select(CareerTournament)
        .where(
            CareerTournament.id == tournament_id,
            CareerTournament.career_athlete_id == athlete_id,
        )
        .options(
            selectinload(CareerTournament.rounds),
            # apply_round_result reads athlete attributes for the next round's
            # field and medal XP — eager-load or MissingGreenlet.
            selectinload(CareerTournament.athlete).selectinload(
                CareerAthlete.attributes
            ),
        )
    )
    if for_update:
        # Lock the tournament row only (selectinload runs separate queries, so
        # a join-wide FOR UPDATE is neither needed nor valid here).
        query = query.with_for_update(of=CareerTournament)
    return (await session.execute(query)).scalar_one_or_none()


async def active_tournament(
    session: AsyncSession, athlete_id: int, event_code: str
) -> CareerTournament | None:
    return (
        await session.execute(
            select(CareerTournament).where(
                CareerTournament.career_athlete_id == athlete_id,
                CareerTournament.event_code == event_code,
                CareerTournament.status == "in_progress",
            )
        )
    ).scalar_one_or_none()


async def start_tournament(
    session: AsyncSession, athlete: CareerAthlete, event_code: str
) -> CareerTournament:
    event = EVENTS[event_code]
    tournament = CareerTournament(
        career_athlete_id=athlete.id, event_code=event_code
    )
    session.add(tournament)
    await session.flush()

    ceiling = attribute_ceiling(event, attributes_dict(athlete))
    session.add(
        TournamentRound(
            tournament_id=tournament.id,
            round_name="qualification",
            field=generate_field(
                tournament.id, "qualification", event, ceiling,
                await country_pool(session),
            ),
        )
    )
    await session.flush()
    return tournament


def open_round(tournament: CareerTournament) -> TournamentRound | None:
    """The round awaiting a player result, if any."""
    for round_row in tournament.rounds:
        if round_row.round_name == tournament.current_round and round_row.position is None:
            return round_row
    return None


async def apply_round_result(
    session: AsyncSession,
    tournament: CareerTournament,
    round_row: TournamentRound,
    player_time: float,
    result_id: int,
) -> TournamentRound:
    """Score a validated player run against the stored field and advance.

    Only called with a VALIDATED result — an invalid run never consumes the
    round, so a rejected submission can be retried (the per-athlete rate limit
    caps probing).
    """
    event = EVENTS[tournament.event_code]
    position = position_in_field(player_time, round_row.field, event)
    _, advance_count = ROUND_RULES[round_row.round_name]

    round_row.player_result_id = result_id
    round_row.position = position

    if round_row.round_name == "final":
        round_row.advanced = False
        tournament.status = "completed"
        tournament.final_position = position
        bonus = MEDAL_XP.get(position, 0)
        if bonus:
            athlete = tournament.athlete
            athlete.total_xp += bonus
            athlete.career_stage = stage_for_xp(athlete.total_xp)
    elif position <= (advance_count or 0):
        round_row.advanced = True
        next_round = ROUND_ORDER[ROUND_ORDER.index(round_row.round_name) + 1]
        tournament.current_round = next_round
        ceiling = attribute_ceiling(event, attributes_dict(tournament.athlete))
        session.add(
            TournamentRound(
                tournament_id=tournament.id,
                round_name=next_round,
                field=generate_field(
                    tournament.id, next_round, event, ceiling,
                    await country_pool(session),
                ),
            )
        )
    else:
        round_row.advanced = False
        tournament.status = "eliminated"

    await session.flush()
    return round_row
