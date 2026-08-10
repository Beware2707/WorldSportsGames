import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/insight_models.dart';
import '../domain/models.dart';
import 'repositories.dart';

abstract class InsightsRepository {
  Future<AIInsight> athleteSummary(String slug);
  Future<AIInsight> athleteTrend(String slug);
  Future<AIInsight> headToHead(String a, String b);
  Future<AIInsight> eventPreview(int eventId);
  Future<Paged<MediaItem>> news({String? sport, String? athlete, int page});
  Future<Paged<MediaItem>> videos({String? sport, String? athlete, int page});
  Future<Paged<AppNotification>> notifications({bool unreadOnly, int page});
  Future<int> unreadCount();
  Future<void> markRead(int id);
  Future<void> markAllRead();
}

class ApiInsightsRepository implements InsightsRepository {
  ApiInsightsRepository(this._client);

  final ApiClient _client;

  @override
  Future<AIInsight> athleteSummary(String slug) async => AIInsight.fromJson(
      await _client.getJson('/api/v1/ai/athletes/$slug/summary'));

  @override
  Future<AIInsight> athleteTrend(String slug) async => AIInsight.fromJson(
      await _client.getJson('/api/v1/ai/athletes/$slug/trend'));

  @override
  Future<AIInsight> headToHead(String a, String b) async =>
      AIInsight.fromJson(await _client
          .getJson('/api/v1/ai/head-to-head', query: {'a': a, 'b': b}));

  @override
  Future<AIInsight> eventPreview(int eventId) async => AIInsight.fromJson(
      await _client.getJson('/api/v1/ai/events/$eventId/preview'));

  @override
  Future<Paged<MediaItem>> news({String? sport, String? athlete, int page = 1}) async =>
      Paged.fromJson(
        await _client.getJson('/api/v1/news',
            query: {'page': page, 'sport': ?sport, 'athlete': ?athlete}),
        MediaItem.fromJson,
      );

  @override
  Future<Paged<MediaItem>> videos({String? sport, String? athlete, int page = 1}) async =>
      Paged.fromJson(
        await _client.getJson('/api/v1/videos',
            query: {'page': page, 'sport': ?sport, 'athlete': ?athlete}),
        MediaItem.fromJson,
      );

  @override
  Future<Paged<AppNotification>> notifications({
    bool unreadOnly = false,
    int page = 1,
  }) async =>
      Paged.fromJson(
        await _client.getJson('/api/v1/notifications',
            query: {'page': page, 'unread_only': unreadOnly}),
        AppNotification.fromJson,
      );

  @override
  Future<int> unreadCount() async {
    final json = await _client.getJson('/api/v1/notifications/unread-count');
    return json['unread'] as int;
  }

  @override
  Future<void> markRead(int id) =>
      _client.postJson('/api/v1/notifications/$id/read');

  @override
  Future<void> markAllRead() => _client.postEmpty('/api/v1/notifications/read-all');
}

final insightsRepositoryProvider = Provider<InsightsRepository>(
  (ref) => ApiInsightsRepository(ref.watch(apiClientProvider)),
);
