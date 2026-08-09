from datetime import date

from sqlalchemy import ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.catalog import Country, Sport

COMPETITION_LEVELS = ("olympic", "world", "continental", "league", "national", "other")
EDITION_STATUSES = ("upcoming", "live", "completed")


class Competition(Base):
    __tablename__ = "competition"

    id: Mapped[int] = mapped_column(primary_key=True)
    slug: Mapped[str] = mapped_column(String(160), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(200))
    level: Mapped[str] = mapped_column(String(16), index=True)
    # NULL sport = multi-sport event (Olympic Games, etc.)
    sport_id: Mapped[int | None] = mapped_column(ForeignKey("sport.id"), index=True)

    sport: Mapped[Sport | None] = relationship()
    editions: Mapped[list["CompetitionEdition"]] = relationship(
        back_populates="competition", order_by="CompetitionEdition.year.desc()"
    )


class CompetitionEdition(Base):
    __tablename__ = "competition_edition"
    __table_args__ = (UniqueConstraint("competition_id", "label", name="uq_edition_label"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    competition_id: Mapped[int] = mapped_column(ForeignKey("competition.id"), index=True)
    label: Mapped[str] = mapped_column(String(160))
    year: Mapped[int] = mapped_column(index=True)
    start_date: Mapped[date | None]
    end_date: Mapped[date | None]
    host_city: Mapped[str | None] = mapped_column(String(128))
    host_country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"))
    status: Mapped[str] = mapped_column(String(16), default="upcoming", index=True)

    competition: Mapped[Competition] = relationship(back_populates="editions")
    host_country: Mapped[Country | None] = relationship()
