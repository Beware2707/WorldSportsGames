"""Rankings, records and medals.

Each sport ranks and records differently, so nothing here assumes a shared
scoring system: rankings carry an explicit ``methodology``, and records carry
both a canonical display string and a comparable magnitude.
"""

from datetime import date

from sqlalchemy import Date, ForeignKey, Index, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.catalog import Country, Discipline, Sport
from app.models.competition import CompetitionEdition

# Who/what is ranked. Kept open so team and country ladders reuse one table.
RANKING_SCOPES = ("athlete", "team", "country")

# How a ladder is computed. Never assume every sport uses points.
RANKING_METHODOLOGIES = (
    "world_ranking",
    "olympic_ranking",
    "season_points",
    "elo",
    "medal_count",
)

RECORD_KINDS = ("WR", "OR", "CR", "AR", "NR", "PB", "SB")
MEDAL_METALS = ("gold", "silver", "bronze")


class Venue(Base):
    __tablename__ = "venue"

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(200))
    city: Mapped[str | None] = mapped_column(String(128))
    country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"), index=True)

    country: Mapped[Country | None] = relationship()


class Team(Base):
    """A national or club side. Teams compete alongside athletes, so medal and
    ranking rows reference either."""

    __tablename__ = "team"
    __table_args__ = (UniqueConstraint("sport_id", "name", name="uq_team_sport_name"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(160))
    sport_id: Mapped[int] = mapped_column(ForeignKey("sport.id"), index=True)
    country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"), index=True)

    sport: Mapped[Sport] = relationship()
    country: Mapped[Country | None] = relationship()


class Ranking(Base):
    """One row of one ladder.

    ``entity_id`` is polymorphic on ``scope`` (athlete/team/country), matching
    the Favorite pattern; the service layer validates the target exists.
    """

    __tablename__ = "ranking"
    __table_args__ = (
        UniqueConstraint(
            "scope", "methodology", "discipline_id", "entity_id", "as_of",
            name="uq_ranking_entry",
        ),
        Index("ix_ranking_ladder", "scope", "methodology", "discipline_id", "as_of"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    scope: Mapped[str] = mapped_column(String(16))
    methodology: Mapped[str] = mapped_column(String(32))
    # A ladder is per-discipline, or per-sport when the sport has no split.
    discipline_id: Mapped[int | None] = mapped_column(
        ForeignKey("discipline.id"), index=True
    )
    sport_id: Mapped[int | None] = mapped_column(ForeignKey("sport.id"), index=True)
    entity_id: Mapped[int] = mapped_column()
    rank: Mapped[int] = mapped_column(index=True)
    points: Mapped[float | None]
    as_of: Mapped[date] = mapped_column(Date)

    discipline: Mapped[Discipline | None] = relationship()
    sport: Mapped[Sport | None] = relationship()


class Record(Base):
    """A best-ever mark.

    ``value_text`` is the canonical display string ("9.58", "2:01:09"),
    ``value_num`` the comparable magnitude for sorting. Holder is an athlete
    or a team; ``country_id`` is the representing nation either way.
    """

    __tablename__ = "record"
    __table_args__ = (
        Index("ix_record_lookup", "kind", "discipline_id", "gender"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    kind: Mapped[str] = mapped_column(String(4), index=True)
    discipline_id: Mapped[int] = mapped_column(ForeignKey("discipline.id"), index=True)
    event_name: Mapped[str] = mapped_column(String(160))
    gender: Mapped[str] = mapped_column(String(1))

    athlete_id: Mapped[int | None] = mapped_column(ForeignKey("athlete.id"), index=True)
    team_id: Mapped[int | None] = mapped_column(ForeignKey("team.id"), index=True)
    country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"), index=True)

    value_kind: Mapped[str] = mapped_column(String(16))
    value_num: Mapped[float | None]
    value_text: Mapped[str] = mapped_column(String(64))
    unit: Mapped[str | None] = mapped_column(String(16))
    set_on: Mapped[date | None] = mapped_column(Date)
    location: Mapped[str | None] = mapped_column(String(160))
    edition_id: Mapped[int | None] = mapped_column(
        ForeignKey("competition_edition.id"), index=True
    )

    discipline: Mapped[Discipline] = relationship()
    country: Mapped[Country | None] = relationship()
    edition: Mapped[CompetitionEdition | None] = relationship()


class Medal(Base):
    __tablename__ = "medal"
    __table_args__ = (
        Index("ix_medal_edition_country", "edition_id", "country_id"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    edition_id: Mapped[int] = mapped_column(
        ForeignKey("competition_edition.id"), index=True
    )
    event_id: Mapped[int | None] = mapped_column(ForeignKey("event.id"), index=True)
    discipline_id: Mapped[int | None] = mapped_column(ForeignKey("discipline.id"))
    event_name: Mapped[str] = mapped_column(String(160))
    metal: Mapped[str] = mapped_column(String(8), index=True)

    athlete_id: Mapped[int | None] = mapped_column(ForeignKey("athlete.id"), index=True)
    team_id: Mapped[int | None] = mapped_column(ForeignKey("team.id"), index=True)
    country_id: Mapped[int] = mapped_column(ForeignKey("country.id"), index=True)

    country: Mapped[Country] = relationship()
    edition: Mapped[CompetitionEdition] = relationship()
    discipline: Mapped[Discipline | None] = relationship()
