import 'dart:convert';

import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/data/personalization_repository.dart';
import 'package:world_sports_games/domain/personalization_models.dart';

/// Wire-level tests for ApiPersonalizationRepository.
///
/// The screen tests stub at the repository interface, so URLs, HTTP verbs and
/// JSON keys had no coverage at all — exactly the layer that breaks silently
/// when the backend contract shifts. These assert the real request/response
/// shapes the FastAPI routes produce.
class _RecordingAdapter implements HttpClientAdapter {
  final requests = <RequestOptions>[];
  Object? nextBody;
  int status = 200;

  @override
  Future<ResponseBody> fetch(
      RequestOptions options, Stream<List<int>>? _, Future<void>? _) async {
    requests.add(options);
    return ResponseBody.fromString(
      jsonEncode(nextBody ?? []),
      status,
      headers: {
        Headers.contentTypeHeader: [Headers.jsonContentType]
      },
    );
  }

  @override
  void close({bool force = false}) {}
}

({ApiPersonalizationRepository repo, _RecordingAdapter adapter}) _subject() {
  final dio = Dio(BaseOptions(baseUrl: 'http://test'));
  final adapter = _RecordingAdapter();
  dio.httpClientAdapter = adapter;
  return (
    repo: ApiPersonalizationRepository(ApiClient(dio: dio)),
    adapter: adapter
  );
}

void main() {
  test('follows() GETs the favorites route and parses the list body', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = [
      {
        'entity_type': 'athlete',
        'entity_id': 4,
        'name': 'Zellie Dunbar',
        'subtitle': 'Jamaica',
        'slug': 'zellie-dunbar',
      },
    ];

    final follows = await repo.follows();

    expect(adapter.requests.single.method, 'GET');
    expect(adapter.requests.single.path, '/api/v1/users/me/favorites');
    expect(follows.single.kind, FollowKind.athlete);
    expect(follows.single.entityId, 4);
    expect(follows.single.name, 'Zellie Dunbar');
    expect(follows.single.slug, 'zellie-dunbar');
  });

  test('follow() POSTs snake_case entity_type/entity_id', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = [];

    await repo.follow(FollowKind.competition, 3);

    final request = adapter.requests.single;
    expect(request.method, 'POST');
    expect(request.path, '/api/v1/users/me/favorites');
    expect(request.data, {'entity_type': 'competition', 'entity_id': 3});
  });

  test('unfollow() DELETEs the type/id path form', () async {
    final (repo: repo, adapter: adapter) = _subject();

    await repo.unfollow(FollowKind.sport, 12);

    expect(adapter.requests.single.method, 'DELETE');
    expect(
        adapter.requests.single.path, '/api/v1/users/me/favorites/sport/12');
  });

  test('setFollows() PUTs the {favorites: [...]} envelope', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = [];

    await repo.setFollows(const [
      Follow(kind: FollowKind.sport, entityId: 1),
      Follow(kind: FollowKind.country, entityId: 5),
    ]);

    final request = adapter.requests.single;
    expect(request.method, 'PUT');
    expect(request.data, {
      'favorites': [
        {'entity_type': 'sport', 'entity_id': 1},
        {'entity_type': 'country', 'entity_id': 5},
      ]
    });
  });

  test('an unknown entity_type is skipped, not fatal', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = [
      {'entity_type': 'stadium', 'entity_id': 1, 'name': 'Future kind'},
      {'entity_type': 'sport', 'entity_id': 2, 'name': 'Athletics'},
    ];

    final follows = await repo.follows();

    expect(follows.map((f) => f.entityId), [2],
        reason: 'a newer server must not break the follow list');
  });

  test('suggest() reads the {query, items} envelope', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = {
      'query': 'ath',
      'items': [
        {
          'kind': 'sport',
          'id': 2,
          'label': 'Athletics',
          'sublabel': 'Summer Games',
          'slug': 'athletics',
        },
      ],
    };

    final items = await repo.suggest('ath');

    expect(adapter.requests.single.queryParameters, {'q': 'ath'});
    expect(items.single.label, 'Athletics');
    expect(items.single.sublabel, 'Summer Games');
  });

  test('notification settings round-trip the {preferences: [...]} envelope',
      () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = [
      {'kind': 'medal_result', 'enabled': false},
    ];

    final updated = await repo.updateNotificationSettings(
      const [NotificationSetting(kind: 'medal_result', enabled: false)],
    );

    expect(adapter.requests.single.method, 'PUT');
    expect(adapter.requests.single.data, {
      'preferences': [
        {'kind': 'medal_result', 'enabled': false}
      ]
    });
    expect(updated.single.enabled, isFalse);
  });

  test('onboarding state parses snake_case follow_count', () async {
    final (repo: repo, adapter: adapter) = _subject();
    adapter.nextBody = {'completed': true, 'follow_count': 7};

    final state = await repo.onboardingState();

    expect(state.completed, isTrue);
    expect(state.followCount, 7);
  });
}
