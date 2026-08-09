from app.models.athlete import Athlete, athlete_discipline
from app.models.catalog import Country, Discipline, Sport
from app.models.competition import Competition, CompetitionEdition
from app.models.user import AppUser, Favorite

__all__ = [
    "AppUser",
    "Athlete",
    "Competition",
    "CompetitionEdition",
    "Country",
    "Discipline",
    "Favorite",
    "Sport",
    "athlete_discipline",
]
