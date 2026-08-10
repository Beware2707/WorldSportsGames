import 'dart:async';

import 'package:world_sports_games/data/live_repository.dart';
import 'package:world_sports_games/data/repositories.dart';
import 'package:world_sports_games/data/token_store.dart';
import 'package:world_sports_games/domain/event_models.dart';
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
  Future<Competition> getCompetition(String slug) async {
    _maybeThrow();
    return Competition(
      id: 1,
      slug: slug,
      name: 'Olympic Games',
      level: 'olympic',
      editions: const [
        Edition(id: 2, label: 'LA28', year: 2028, status: 'upcoming'),
        Edition(id: 1, label: 'Paris 2024', year: 2024, status: 'completed'),
      ],
    );
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

const fakeLiveEvent = LiveCoverage(
  event: SportEvent(
    id: 42,
    name: "Women's 100m — Round 1",
    gender: 'F',
    phase: 'heat',
    status: 'live',
  ),
  editionLabel: 'LA28',
  competitionName: 'Olympic Games',
  competitionSlug: 'olympic-games',
  currentPhase: 'heat',
  lastSeq: 3,
);

class FakeEventsRepository implements EventsRepository {
  FakeEventsRepository({this.live = const [], this.failWith});

  List<LiveCoverage> live;
  final Exception? failWith;

  void _maybeThrow() {
    final e = failWith;
    if (e != null) throw e;
  }

  @override
  Future<Paged<SportEvent>> listEvents(
      {int? editionId, String? status, int page = 1}) async {
    _maybeThrow();
    return const Paged(items: [], total: 0, page: 1, pages: 1);
  }

  @override
  Future<List<SportEvent>> allEventsForEdition(int editionId) async {
    _maybeThrow();
    return const [];
  }

  @override
  Future<SportEventDetail> eventDetail(int id) async {
    _maybeThrow();
    return SportEventDetail(
      id: id,
      name: "Women's 100m Final",
      gender: 'F',
      phase: 'final',
      status: 'completed',
      competitionName: 'Olympic Games',
      competitionSlug: 'olympic-games',
      results: [
        EventResult(
          athlete: const Athlete(
              id: 1, slug: 'zellie-dunbar', fullName: 'Zellie Dunbar'),
          position: 1,
          status: 'ok',
          valueKind: 'time',
          valueText: '10.71',
        ),
        EventResult(
          athlete: const Athlete(
              id: 2, slug: 'harper-quinlan', fullName: 'Harper Quinlan'),
          position: null,
          status: 'DNF',
          valueKind: 'time',
          valueText: null,
        ),
      ],
    );
  }

  @override
  Future<List<LiveCoverage>> liveEvents() async {
    _maybeThrow();
    return live;
  }
}

/// Test double for the live WebSocket: push [LiveSocketMessage]s manually.
class FakeLiveSocket implements LiveSocket {
  final StreamController<LiveSocketMessage> controller =
      StreamController<LiveSocketMessage>.broadcast();
  bool closed = false;

  @override
  Stream<LiveSocketMessage> connect() => controller.stream;

  @override
  void close() {
    closed = true;
  }
}

/// In-memory TokenStore for auth tests.
class FakeTokenStore implements TokenStore {
  FakeTokenStore([this.token]);

  String? token;
  bool readThrows = false;

  @override
  Future<String?> read() async {
    if (readThrows) throw Exception('secure storage unavailable');
    return token;
  }

  @override
  Future<void> write(String value) async => token = value;

  @override
  Future<void> clear() async => token = null;
}
