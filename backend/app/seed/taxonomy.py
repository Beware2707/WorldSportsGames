"""Olympic sport/discipline taxonomy.

This is *reference data*, not mock data: the catalogue of sports and
disciplines the platform covers. Sports with no listed sub-disciplines get a
single default discipline mirroring the sport, so events always attach to a
discipline uniformly.

Format: (code, name, category, [(discipline_code, discipline_name), ...])
"""

SPORTS: list[tuple[str, str, str, list[tuple[str, str]]]] = [
    # ---- Summer ----
    ("aquatics", "Aquatics", "summer", [
        ("swimming", "Swimming"),
        ("diving", "Diving"),
        ("artistic-swimming", "Artistic Swimming"),
        ("water-polo", "Water Polo"),
        ("marathon-swimming", "Marathon Swimming"),
    ]),
    ("athletics", "Athletics", "summer", [
        ("track-field", "Track & Field"),
        ("road-running", "Road Running"),
        ("marathon", "Marathon"),
        ("race-walking", "Race Walking"),
        ("cross-country", "Cross Country"),
    ]),
    ("archery", "Archery", "summer", []),
    ("badminton", "Badminton", "summer", []),
    ("basketball", "Basketball", "summer", [
        ("basketball", "Basketball"),
        ("basketball-3x3", "3x3 Basketball"),
    ]),
    ("boxing", "Boxing", "summer", []),
    ("breaking", "Breaking", "summer", []),
    ("canoe", "Canoe", "summer", [
        ("canoe-sprint", "Canoe Sprint"),
        ("canoe-slalom", "Canoe Slalom"),
    ]),
    ("cycling", "Cycling", "summer", [
        ("road-cycling", "Road Cycling"),
        ("track-cycling", "Track Cycling"),
        ("mountain-bike", "Mountain Bike"),
        ("bmx-racing", "BMX Racing"),
        ("bmx-freestyle", "BMX Freestyle"),
    ]),
    ("equestrian", "Equestrian", "summer", [
        ("dressage", "Dressage"),
        ("eventing", "Eventing"),
        ("jumping", "Jumping"),
    ]),
    ("fencing", "Fencing", "summer", []),
    ("football", "Football", "summer", []),
    ("golf", "Golf", "summer", []),
    ("gymnastics", "Gymnastics", "summer", [
        ("artistic-gymnastics", "Artistic Gymnastics"),
        ("rhythmic-gymnastics", "Rhythmic Gymnastics"),
        ("trampoline", "Trampoline"),
    ]),
    ("handball", "Handball", "summer", []),
    ("hockey", "Hockey", "summer", []),
    ("judo", "Judo", "summer", []),
    ("modern-pentathlon", "Modern Pentathlon", "summer", []),
    ("rowing", "Rowing", "summer", []),
    ("rugby-sevens", "Rugby Sevens", "summer", []),
    ("sailing", "Sailing", "summer", []),
    ("shooting", "Shooting", "summer", []),
    ("skateboarding", "Skateboarding", "summer", []),
    ("sport-climbing", "Sport Climbing", "summer", []),
    ("surfing", "Surfing", "summer", []),
    ("table-tennis", "Table Tennis", "summer", []),
    ("taekwondo", "Taekwondo", "summer", []),
    ("tennis", "Tennis", "summer", []),
    ("triathlon", "Triathlon", "summer", []),
    ("volleyball", "Volleyball", "summer", [
        ("volleyball-indoor", "Volleyball"),
        ("beach-volleyball", "Beach Volleyball"),
    ]),
    ("weightlifting", "Weightlifting", "summer", []),
    ("wrestling", "Wrestling", "summer", [
        ("freestyle-wrestling", "Freestyle"),
        ("greco-roman-wrestling", "Greco-Roman"),
    ]),
    # ---- LA28 additions ----
    ("baseball", "Baseball", "la28", []),
    ("softball", "Softball", "la28", []),
    ("cricket", "Cricket", "la28", []),
    ("flag-football", "Flag Football", "la28", []),
    ("lacrosse", "Lacrosse", "la28", []),
    ("squash", "Squash", "la28", []),
    # ---- Winter ----
    ("alpine-skiing", "Alpine Skiing", "winter", []),
    ("biathlon", "Biathlon", "winter", []),
    ("bobsleigh", "Bobsleigh", "winter", []),
    ("cross-country-skiing", "Cross-Country Skiing", "winter", []),
    ("curling", "Curling", "winter", []),
    ("figure-skating", "Figure Skating", "winter", []),
    ("freestyle-skiing", "Freestyle Skiing", "winter", []),
    ("ice-hockey", "Ice Hockey", "winter", []),
    ("luge", "Luge", "winter", []),
    ("nordic-combined", "Nordic Combined", "winter", []),
    ("short-track", "Short Track Speed Skating", "winter", []),
    ("skeleton", "Skeleton", "winter", []),
    ("ski-jumping", "Ski Jumping", "winter", []),
    ("ski-mountaineering", "Ski Mountaineering", "winter", []),
    ("snowboard", "Snowboard", "winter", []),
    ("speed-skating", "Speed Skating", "winter", []),
]

# Material Symbols icon names the Flutter client maps to real icons.
ICONS: dict[str, str] = {
    "aquatics": "pool",
    "athletics": "sprint",
    "basketball": "sports_basketball",
    "football": "sports_soccer",
    "tennis": "sports_tennis",
    "cricket": "sports_cricket",
    "golf": "sports_golf",
    "volleyball": "sports_volleyball",
    "handball": "sports_handball",
    "hockey": "sports_hockey",
    "boxing": "sports_mma",
    "cycling": "directions_bike",
    "rowing": "rowing",
    "sailing": "sailing",
    "ice-hockey": "sports_hockey",
    "alpine-skiing": "downhill_skiing",
    "snowboard": "snowboarding",
    "figure-skating": "ice_skating",
    "surfing": "surfing",
    "skateboarding": "skateboarding",
    "sport-climbing": "hiking",
}
