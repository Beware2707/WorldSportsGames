from datetime import date

from sqlalchemy import Column, ForeignKey, String, Table
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.models.catalog import Country, Discipline

# An athlete may compete in several disciplines (e.g. 100m and long jump).
athlete_discipline = Table(
    "athlete_discipline",
    Base.metadata,
    Column("athlete_id", ForeignKey("athlete.id"), primary_key=True),
    Column("discipline_id", ForeignKey("discipline.id"), primary_key=True),
)


class Athlete(Base):
    __tablename__ = "athlete"

    id: Mapped[int] = mapped_column(primary_key=True)
    slug: Mapped[str] = mapped_column(String(160), unique=True, index=True)
    given_name: Mapped[str] = mapped_column(String(96))
    family_name: Mapped[str] = mapped_column(String(96))
    country_id: Mapped[int | None] = mapped_column(ForeignKey("country.id"), index=True)
    date_of_birth: Mapped[date | None]
    sex: Mapped[str | None] = mapped_column(String(1))
    headshot_url: Mapped[str | None] = mapped_column(String(512))
    bio: Mapped[str | None] = mapped_column(String(2000))
    is_active: Mapped[bool] = mapped_column(default=True)

    country: Mapped[Country | None] = relationship()
    disciplines: Mapped[list[Discipline]] = relationship(secondary=athlete_discipline)

    @property
    def full_name(self) -> str:
        return f"{self.given_name} {self.family_name}".strip()
