import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/personalization_models.dart';
import 'repositories.dart';

/// Follows, onboarding state and notification preferences.
abstract class PersonalizationRepository {
  Future<List<Follow>> follows();
  Future<List<Follow>> follow(FollowKind kind, int entityId);
  Future<void> unfollow(FollowKind kind, int entityId);
  Future<List<Follow>> setFollows(List<Follow> follows);
  Future<OnboardingState> onboardingState();
  Future<List<NotificationSetting>> notificationSettings();
  Future<List<NotificationSetting>> updateNotificationSettings(
      List<NotificationSetting> settings);
  Future<List<SearchSuggestion>> suggest(String query);
}

class ApiPersonalizationRepository implements PersonalizationRepository {
  ApiPersonalizationRepository(this._client);

  final ApiClient _client;

  // Unknown follow kinds from a newer server are skipped rather than
  // crashing the follow list.
  List<Follow> _parseFollows(List<dynamic> raw) => raw
      .map((e) => Follow.fromJson(e as Map<String, dynamic>))
      .whereType<Follow>()
      .toList();

  @override
  Future<List<Follow>> follows() async =>
      _parseFollows(await _client.getJsonList('/api/v1/users/me/favorites'));

  @override
  Future<List<Follow>> follow(FollowKind kind, int entityId) async =>
      _parseFollows(await _client.postJsonList(
        '/api/v1/users/me/favorites',
        body: {'entity_type': kind.name, 'entity_id': entityId},
      ));

  @override
  Future<void> unfollow(FollowKind kind, int entityId) =>
      _client.delete('/api/v1/users/me/favorites/${kind.name}/$entityId');

  @override
  Future<List<Follow>> setFollows(List<Follow> follows) async =>
      _parseFollows(await _client.putJsonList(
        '/api/v1/users/me/favorites',
        body: {'favorites': follows.map((f) => f.toJson()).toList()},
      ));

  @override
  Future<OnboardingState> onboardingState() async => OnboardingState.fromJson(
      await _client.getJson('/api/v1/users/me/onboarding'));

  @override
  Future<List<NotificationSetting>> notificationSettings() async =>
      (await _client.getJsonList('/api/v1/users/me/notifications'))
          .map((e) => NotificationSetting.fromJson(e as Map<String, dynamic>))
          .toList();

  @override
  Future<List<NotificationSetting>> updateNotificationSettings(
          List<NotificationSetting> settings) async =>
      (await _client.putJsonList(
        '/api/v1/users/me/notifications',
        body: {'preferences': settings.map((s) => s.toJson()).toList()},
      ))
          .map((e) => NotificationSetting.fromJson(e as Map<String, dynamic>))
          .toList();

  @override
  Future<List<SearchSuggestion>> suggest(String query) async {
    final json =
        await _client.getJson('/api/v1/search/suggest', query: {'q': query});
    return (json['items'] as List)
        .map((e) => SearchSuggestion.fromJson(e as Map<String, dynamic>))
        .toList();
  }
}

final personalizationRepositoryProvider = Provider<PersonalizationRepository>(
  (ref) => ApiPersonalizationRepository(ref.watch(apiClientProvider)),
);
