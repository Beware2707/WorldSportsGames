import 'package:world_sports_games/data/repositories.dart';
import 'package:world_sports_games/domain/models.dart';

class FakeCatalogRepository implements CatalogRepository {
  FakeCatalogRepository({this.failWith});

  final Exception? failWith;

  static const sports = [
    Sport(id: 1, code: 'athletics', name: 'Athletics', category: 'summer',
        icon: 'sprint'),
    Sport(id: 2, code: 'curling', name: 'Curling', category: 'winter'),
  ];

  void _maybeThrow() {
    final e = failWith;
    if (e != null) throw e;
  }

  @override
  Future<List<Sport>> listSports({String? category}) async {
    _maybeThrow();
    return category == null
        ? sports
        : sports.where((s) => s.category == category).toList();
  }

  @override
  Future<Sport> getSport(String code) async {
    _maybeThrow();
    return const Sport(
      id: 1,
      code: 'athletics',
      name: 'Athletics',
      category: 'summer',
      disciplines: [
        Discipline(id: 1, code: 'track-field', name: 'Track & Field'),
        Discipline(id: 2, code: 'marathon', name: 'Marathon'),
      ],
    );
  }

  @override
  Future<Paged<Athlete>> listAthletes({String? sport, int page = 1}) async {
    _maybeThrow();
    return const Paged(items: [], total: 0, page: 1, pages: 1);
  }

  @override
  Future<Paged<Competition>> listCompetitions(
      {String? level, int page = 1}) async {
    _maybeThrow();
    return const Paged(items: [], total: 0, page: 1, pages: 1);
  }

  @override
  Future<List<HomeSection>> homeFeed() async {
    _maybeThrow();
    return const [
      // Deliberately empty: the UI must render an honest empty state,
      // never invent live events.
      HomeSection(kind: 'live_now', title: 'Live Now', items: []),
      HomeSection(kind: 'up_next', title: 'Up Next', items: [
        {
          'id': 1,
          'label': 'LA28',
          'year': 2028,
          'status': 'upcoming',
          'start_date': '2028-07-14',
          'host_city': 'Los Angeles',
          'host_country': null,
          'competition': {
            'id': 1,
            'slug': 'olympic-games',
            'name': 'Olympic Games',
            'level': 'olympic',
            'sport': null,
          },
        },
      ]),
      HomeSection(kind: 'athlete_spotlight', title: 'Athlete Spotlight', items: [
        {
          'id': 7,
          'slug': 'amara-okafor',
          'full_name': 'Amara Okafor',
          'headshot_url': null,
          'country': {'id': 1, 'iso3': 'USA', 'name': 'United States',
            'flag_emoji': '🇺🇸'},
        },
      ]),
    ];
  }
}
