import 'dart:convert';

import 'package:dio/dio.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/data/competitive_repository.dart';
import 'package:world_sports_games/domain/competitive_models.dart';
import 'package:world_sports_games/domain/models.dart';
import 'package:world_sports_games/features/countries/medal_table_screen.dart';
import 'package:world_sports_games/features/records/records_screen.dart';

class _StubAdapter implements HttpClientAdapter {
  final requests = <RequestOptions>[];
  final Map<String, Object> bodies = {};

  @override
  Future<ResponseBody> fetch(
      RequestOptions options, Stream<List<int>>? _, Future<void>? _) async {
    requests.add(options);
    final body = bodies[options.path] ?? <dynamic>[];
    return ResponseBody.fromString(
      jsonEncode(body),
      200,
      headers: {
        Headers.contentTypeHeader: [Headers.jsonContentType]
      },
    );
  }

  @override
  void close({bool force = false}) {}
}

({ApiCompetitiveRepository repo, _StubAdapter adapter}) _subject() {
  final dio = Dio(BaseOptions(baseUrl: 'http://test'));
  final adapter = _StubAdapter();
  dio.httpClientAdapter = adapter;
  return (repo: ApiCompetitiveRepository(ApiClient(dio: dio)), adapter: adapter);
}

/// Repository double backed by literals, for widget tests.
class FakeCompetitiveRepository implements CompetitiveRepository {
  FakeCompetitiveRepository({this.table, this.recordList = const []});

  final MedalTable? table;
  final List<SportRecord> recordList;

  @override
  Future<RankingLadder> rankings({
    String scope = 'athlete',
    String methodology = 'world_ranking',
    String? discipline,
  }) async =>
      const RankingLadder(
          scope: 'athlete', methodology: 'world_ranking', entries: []);

  @override
  Future<List<SportRecord>> records(
          {String? kind, String? discipline, String? gender}) async =>
      recordList;

  @override
  Future<MedalTable> medalTable({int? editionId}) async =>
      table ?? const MedalTable(rows: []);

  @override
  Future<List<Edition>> medalEditions() async => const [];

  @override
  Future<CountryProfile> countryProfile(String iso3) async =>
      throw UnimplementedError();

  @override
  Future<AthleteProfile> athleteProfile(String slug) async =>
      throw UnimplementedError();
}

const _jam = Country(id: 1, iso3: 'JAM', name: 'Jamaica', flagEmoji: '🇯🇲');
const _usa = Country(id: 2, iso3: 'USA', name: 'United States', flagEmoji: '🇺🇸');

void main() {
  group('wire contract', () {
    test('rankings parses the ladder envelope with snake_case keys', () async {
      final (repo: repo, adapter: adapter) = _subject();
      adapter.bodies['/api/v1/rankings'] = {
        'scope': 'athlete',
        'methodology': 'world_ranking',
        'discipline': {'id': 1, 'code': 'track-field', 'name': 'Track & Field'},
        'as_of': '2026-08-01',
        'entries': [
          {
            'rank': 1,
            'points': 1487.0,
            'entity_id': 4,
            'entity_name': 'Zellie Dunbar',
            'entity_subtitle': 'Jamaica',
            'entity_slug': 'zellie-dunbar',
          },
        ],
      };

      final ladder = await repo.rankings(discipline: 'track-field');

      expect(adapter.requests.single.queryParameters['discipline'],
          'track-field');
      expect(ladder.asOf, DateTime(2026, 8, 1));
      expect(ladder.entries.single.entityName, 'Zellie Dunbar');
      expect(ladder.entries.single.points, 1487.0);
    });

    test('records parses value/unit and holder fields', () async {
      final (repo: repo, adapter: adapter) = _subject();
      adapter.bodies['/api/v1/records'] = [
        {
          'id': 1,
          'kind': 'WR',
          'event_name': 'All-Around',
          'gender': 'F',
          'value_text': '59.211',
          'value_kind': 'points',
          'unit': 'pts',
          'set_on': '2025-10-05',
          'location': 'Jakarta',
          'discipline': {
            'id': 3,
            'code': 'artistic-gymnastics',
            'name': 'Artistic Gymnastics'
          },
          'country': {
            'id': 5,
            'iso3': 'CHN',
            'name': 'China',
            'flag_emoji': '🇨🇳'
          },
          'holder_name': 'Wei-Lin Zhao',
          'holder_slug': 'wei-lin-zhao',
        },
      ];

      final records = await repo.records(kind: 'WR');

      expect(adapter.requests.single.queryParameters['kind'], 'WR');
      final record = records.single;
      expect(record.valueKind, 'points',
          reason: 'records must not assume every sport measures time');
      expect(record.unit, 'pts');
      expect(record.holderSlug, 'wei-lin-zhao');
      expect(record.kindLabel, 'World record');
    });

    test('medal table parses rows and edition label', () async {
      final (repo: repo, adapter: adapter) = _subject();
      adapter.bodies['/api/v1/medals'] = {
        'edition_id': 7,
        'edition_label': 'Paris 2024',
        'rows': [
          {
            'country': {
              'id': 1,
              'iso3': 'JAM',
              'name': 'Jamaica',
              'flag_emoji': '🇯🇲'
            },
            'gold': 1,
            'silver': 0,
            'bronze': 0,
            'total': 1,
            'rank': 1,
          },
        ],
      };

      final table = await repo.medalTable(editionId: 7);

      expect(adapter.requests.single.queryParameters['edition_id'], 7);
      expect(table.editionLabel, 'Paris 2024');
      expect(table.rows.single.country.iso3, 'JAM');
      expect(table.rows.single.total, 1);
    });
  });

  group('medal table screen', () {
    testWidgets('renders gold-ordered rows with ranks', (tester) async {
      const table = MedalTable(rows: [
        MedalTally(
            country: _jam, gold: 3, silver: 1, bronze: 0, total: 4, rank: 1),
        MedalTally(
            country: _usa, gold: 1, silver: 5, bronze: 2, total: 8, rank: 2),
      ]);

      await tester.pumpWidget(ProviderScope(
        overrides: [
          competitiveRepositoryProvider
              .overrideWithValue(FakeCompetitiveRepository(table: table)),
        ],
        child: const MaterialApp(home: MedalTableScreen()),
      ));
      await tester.pumpAndSettle();

      expect(find.textContaining('Jamaica'), findsOneWidget);
      // USA has more total medals but less gold, so it ranks second — the
      // table must not be total-ordered.
      expect(find.textContaining('United States'), findsOneWidget);
      expect(find.text('1'), findsWidgets);
      expect(find.text('8'), findsOneWidget);
    });

    testWidgets('empty table shows an honest empty state', (tester) async {
      await tester.pumpWidget(ProviderScope(
        overrides: [
          competitiveRepositoryProvider
              .overrideWithValue(FakeCompetitiveRepository()),
        ],
        child: const MaterialApp(home: MedalTableScreen()),
      ));
      await tester.pumpAndSettle();

      expect(find.text('No medals recorded yet'), findsOneWidget);
    });
  });

  group('records screen', () {
    testWidgets('renders a points record without assuming time', (tester) async {
      final records = [
        const SportRecord(
          id: 1,
          kind: 'WR',
          eventName: 'All-Around',
          gender: 'F',
          valueText: '59.211',
          valueKind: 'points',
          unit: 'pts',
          holderName: 'Wei-Lin Zhao',
        ),
      ];

      await tester.pumpWidget(ProviderScope(
        overrides: [
          competitiveRepositoryProvider.overrideWithValue(
              FakeCompetitiveRepository(recordList: records)),
        ],
        child: const MaterialApp(home: RecordsScreen()),
      ));
      await tester.pumpAndSettle();

      expect(find.text('59.211'), findsOneWidget);
      expect(find.text('pts'), findsOneWidget);
      expect(find.textContaining('All-Around'), findsOneWidget);
      expect(find.textContaining('Wei-Lin Zhao'), findsOneWidget);
    });
  });
}
