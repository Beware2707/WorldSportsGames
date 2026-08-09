from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.athlete import Athlete
from app.models.catalog import Discipline
from app.models.competition import CompetitionEdition

EVENT_STATUSES = ("scheduled", "live", "completed", "cancelled")
EVENT_PHASES = ("qualification", "heat", "quarterfinal", "semifinal", "final", "round")
RESULT_STATUSES = ("ok", "DNS", "DNF", "DSQ")
# One normalized result row per athlete; value_kind selects the client renderer.
RESULT_VALUE_KINDS = (
    "time", "distance", "height", "points", "score", "goals", "sets", "rounds",
)


class Event(Base):
    """A single competitive unit: one final, heat, match or round."""

    __tablename__ = "event"

    id: Mapped[int] = mapped_column(primary_key=True)
    edition_id: Mapped[int] = mapped_column(ForeignKey("competition_edition.id"), index=True)
    discipline_id: Mapped[int] = mapped_column(ForeignKey("discipline.id"), index=True)
    name: Mapped[str] = mapped_column(String(200))
    gender: Mapped[str] = mapped_column(String(1))  # M / F / X (mixed)
    phase: Mapped[str] = mapped_column(String(16))
    scheduled_start: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    status: Mapped[str] = mapped_column(String(16), default="scheduled", index=True)

    edition: Mapped[CompetitionEdition] = relationship()
    discipline: Mapped[Discipline] = relationship()
    participations: Mapped[list["Participation"]] = relationship(back_populates="event")


class Participation(Base):
    __tablename__ = "participation"
    __table_args__ = (
        UniqueConstraint("event_id", "athlete_id", name="uq_participation_event_athlete"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    event_id: Mapped[int] = mapped_column(ForeignKey("event.id"), index=True)
    athlete_id: Mapped[int] = mapped_column(ForeignKey("athlete.id"), index=True)
    bib: Mapped[str | None] = mapped_column(String(16))
    lane: Mapped[int | None]

    event: Mapped[Event] = relationship(back_populates="participations")
    athlete: Mapped[Athlete] = relationship()
    result: Mapped["Result | None"] = relationship(back_populates="participation")


class Result(Base):
    """Normalized result: one row per participation.

    ``value_text`` is the canonical display string ("10.71", "2:06:34",
    "57.566"); ``value_num`` is the comparable magnitude (seconds, metres,
    points) for sorting and future analytics. Sport-specific breakdowns
    (splits, set scores) go in ``ResultDetail`` key/values — never new columns.
    """

    __tablename__ = "result"

    id: Mapped[int] = mapped_column(primary_key=True)
    participation_id: Mapped[int] = mapped_column(
        ForeignKey("participation.id"), unique=True, index=True
    )
    position: Mapped[int | None]
    status: Mapped[str] = mapped_column(String(8), default="ok")
    value_kind: Mapped[str] = mapped_column(String(16))
    value_num: Mapped[float | None]
    value_text: Mapped[str | None] = mapped_column(String(64))
    qualified: Mapped[bool | None]

    participation: Mapped[Participation] = relationship(back_populates="result")
    details: Mapped[list["ResultDetail"]] = relationship(back_populates="result")


class ResultDetail(Base):
    __tablename__ = "result_detail"

    id: Mapped[int] = mapped_column(primary_key=True)
    result_id: Mapped[int] = mapped_column(ForeignKey("result.id"), index=True)
    key: Mapped[str] = mapped_column(String(64))
    value: Mapped[str] = mapped_column(String(128))

    result: Mapped[Result] = relationship(back_populates="details")
