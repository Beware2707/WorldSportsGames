from sqlalchemy import ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base

# Sport categories. A plain string column (not a DB enum) so new categories
# never require a migration.
SPORT_CATEGORIES = ("summer", "winter", "la28")


class Sport(Base):
    __tablename__ = "sport"

    id: Mapped[int] = mapped_column(primary_key=True)
    code: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(128))
    category: Mapped[str] = mapped_column(String(16), index=True)
    icon: Mapped[str | None] = mapped_column(String(64))
    sort_order: Mapped[int] = mapped_column(default=0)

    disciplines: Mapped[list["Discipline"]] = relationship(
        back_populates="sport", order_by="Discipline.name"
    )


class Discipline(Base):
    __tablename__ = "discipline"
    __table_args__ = (UniqueConstraint("sport_id", "code", name="uq_discipline_sport_code"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    sport_id: Mapped[int] = mapped_column(ForeignKey("sport.id"), index=True)
    code: Mapped[str] = mapped_column(String(64))
    name: Mapped[str] = mapped_column(String(128))

    sport: Mapped[Sport] = relationship(back_populates="disciplines")


class Country(Base):
    __tablename__ = "country"

    id: Mapped[int] = mapped_column(primary_key=True)
    iso3: Mapped[str] = mapped_column(String(3), unique=True, index=True)
    iso2: Mapped[str] = mapped_column(String(2))
    name: Mapped[str] = mapped_column(String(128))
    flag_emoji: Mapped[str | None] = mapped_column(String(8))
