"""Seed data beyond the sport taxonomy.

Two clearly separated tiers:

- REFERENCE: real-world catalogue facts (countries, major competitions and
  their editions). Safe in any environment.
- DEV FIXTURES: *fictional* athletes for development/demo only. Never real
  people, never seeded unless ``SPORTS_ENABLE_DEV_FIXTURES=true``. Every
  fixture bio is prefixed so it can't be mistaken for real data.
"""

from datetime import date

COUNTRIES: list[tuple[str, str, str, str]] = [
    # (iso3, iso2, name, flag)
    ("USA", "US", "United States", "🇺🇸"),
    ("GBR", "GB", "Great Britain", "🇬🇧"),
    ("KEN", "KE", "Kenya", "🇰🇪"),
    ("JAM", "JM", "Jamaica", "🇯🇲"),
    ("IND", "IN", "India", "🇮🇳"),
    ("AUS", "AU", "Australia", "🇦🇺"),
    ("FRA", "FR", "France", "🇫🇷"),
    ("JPN", "JP", "Japan", "🇯🇵"),
    ("NOR", "NO", "Norway", "🇳🇴"),
    ("BRA", "BR", "Brazil", "🇧🇷"),
    ("CHN", "CN", "China", "🇨🇳"),
    ("ETH", "ET", "Ethiopia", "🇪🇹"),
    ("GER", "DE", "Germany", "🇩🇪"),
    ("ITA", "IT", "Italy", "🇮🇹"),
    ("NED", "NL", "Netherlands", "🇳🇱"),
]

# (slug, name, level, sport_code | None, editions)
# editions: (label, year, start, end, host_city, host_iso3, status)
COMPETITIONS: list[tuple] = [
    (
        "olympic-games", "Olympic Games", "olympic", None,
        [
            ("Paris 2024", 2024, date(2024, 7, 26), date(2024, 8, 11), "Paris", "FRA",
             "completed"),
            ("LA28", 2028, date(2028, 7, 14), date(2028, 7, 30), "Los Angeles", "USA",
             "upcoming"),
        ],
    ),
    (
        "winter-olympic-games", "Winter Olympic Games", "olympic", None,
        [
            ("Milano Cortina 2026", 2026, date(2026, 2, 6), date(2026, 2, 22),
             "Milan", "ITA", "completed"),
            ("French Alps 2030", 2030, None, None, "French Alps", "FRA", "upcoming"),
        ],
    ),
    (
        "world-athletics-championships", "World Athletics Championships", "world", "athletics",
        [
            ("Tokyo 2025", 2025, date(2025, 9, 13), date(2025, 9, 21), "Tokyo", "JPN",
             "completed"),
            ("Beijing 2027", 2027, None, None, "Beijing", "CHN", "upcoming"),
        ],
    ),
    (
        "world-aquatics-championships", "World Aquatics Championships", "world", "aquatics",
        [("Budapest 2027", 2027, None, None, "Budapest", None, "upcoming")],
    ),
    (
        "diamond-league", "Diamond League", "league", "athletics",
        [("2026 Season", 2026, date(2026, 4, 25), date(2026, 9, 5), None, None, "live")],
    ),
    (
        "fifa-world-cup", "FIFA World Cup", "world", "football",
        [("2026", 2026, date(2026, 6, 11), date(2026, 7, 19), None, "USA", "completed")],
    ),
]

# Fictional development athletes — names invented, not real people.
# (slug, given, family, iso3, dob, sex, sport_code, [discipline_codes])
DEV_ATHLETES: list[tuple] = [
    ("amara-okafor", "Amara", "Okafor", "USA", date(1999, 3, 14), "F",
     "athletics", ["track-field"]),
    ("dax-merrow", "Dax", "Merrow", "GBR", date(1997, 11, 2), "M",
     "athletics", ["track-field", "road-running"]),
    ("kiptoo-cherop", "Kiptoo", "Cherop", "KEN", date(2000, 6, 21), "M",
     "athletics", ["marathon", "road-running"]),
    ("zellie-dunbar", "Zellie", "Dunbar", "JAM", date(2001, 1, 9), "F",
     "athletics", ["track-field"]),
    ("ishaan-verma", "Ishaan", "Verma", "IND", date(1998, 8, 30), "M",
     "cricket", []),
    ("marlo-tanaka", "Marlo", "Tanaka", "JPN", date(2002, 4, 17), "F",
     "aquatics", ["swimming"]),
    ("solveig-brandt", "Solveig", "Brandt", "NOR", date(1996, 12, 5), "F",
     "cross-country-skiing", []),
    ("rafael-cintra", "Rafael", "Cintra", "BRA", date(1999, 7, 23), "M",
     "football", []),
    ("wei-lin-zhao", "Wei-Lin", "Zhao", "CHN", date(2003, 2, 11), "F",
     "gymnastics", ["artistic-gymnastics"]),
    ("tadesse-worku", "Tadesse", "Worku", "ETH", date(1997, 5, 19), "M",
     "athletics", ["marathon"]),
    ("colette-marchand", "Colette", "Marchand", "FRA", date(2000, 10, 8), "F",
     "cycling", ["road-cycling", "track-cycling"]),
    ("harper-quinlan", "Harper", "Quinlan", "AUS", date(2001, 9, 27), "F",
     "aquatics", ["swimming", "marathon-swimming"]),
    ("lukas-eberhart", "Lukas", "Eberhart", "GER", date(1995, 3, 3), "M",
     "alpine-skiing", []),
    ("noor-van-dijk", "Noor", "van Dijk", "NED", date(1998, 6, 15), "F",
     "speed-skating", []),
    ("teo-baresi", "Teo", "Baresi", "ITA", date(2002, 11, 20), "M",
     "figure-skating", []),
]

DEV_BIO_PREFIX = "[DEV FIXTURE — fictional athlete for development] "

# Fictional development events/results attached to the fictional athletes.
# (competition_slug, edition_label, discipline_code, name, gender, phase,
#  scheduled_start iso | None, status,
#  [(athlete_slug, lane, position, result_status, value_kind, value_num,
#    value_text)])
# A None position with result_status None means no result recorded yet.
DEV_EVENTS: list[tuple] = [
    (
        "olympic-games", "Paris 2024", "track-field",
        "Women's 100m Final", "F", "final", "2024-08-03T19:20:00+00:00", "completed",
        [
            ("zellie-dunbar", 4, 1, "ok", "time", 10.71, "10.71"),
            ("amara-okafor", 5, 2, "ok", "time", 10.84, "10.84"),
            ("harper-quinlan", 3, None, "DNF", "time", None, None),
        ],
    ),
    (
        "olympic-games", "Paris 2024", "marathon",
        "Men's Marathon", "M", "final", "2024-08-10T06:00:00+00:00", "completed",
        [
            ("kiptoo-cherop", None, 1, "ok", "time", 7594.0, "2:06:34"),
            ("tadesse-worku", None, 2, "ok", "time", 7622.0, "2:07:02"),
            ("dax-merrow", None, 3, "ok", "time", 7784.0, "2:09:44"),
        ],
    ),
    (
        "olympic-games", "Paris 2024", "swimming",
        "Women's 200m Freestyle Final", "F", "final",
        "2024-07-29T18:41:00+00:00", "completed",
        [
            ("marlo-tanaka", 4, 1, "ok", "time", 114.92, "1:54.92"),
            ("harper-quinlan", 5, 2, "ok", "time", 115.30, "1:55.30"),
        ],
    ),
    (
        "olympic-games", "Paris 2024", "artistic-gymnastics",
        "Women's All-Around Final", "F", "final",
        "2024-08-01T16:15:00+00:00", "completed",
        [
            ("wei-lin-zhao", None, 1, "ok", "points", 57.566, "57.566"),
        ],
    ),
    (
        "olympic-games", "LA28", "track-field",
        "Women's 100m — Round 1", "F", "heat", "2028-07-15T17:00:00+00:00", "scheduled",
        [
            ("zellie-dunbar", 4, None, None, None, None, None),
            ("amara-okafor", 5, None, None, None, None, None),
        ],
    ),
]
