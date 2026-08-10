import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/game_models.dart';
import 'repositories.dart';

abstract class GamesRepository {
  Future<List<Game>> games();
  Future<Game> dailyChallenge();
  Future<ScoreResult> submitScore(String code, double score, Map<String, dynamic> detail);
  Future<Leaderboard> leaderboard(String code, {String scope, String? country});
  Future<GameProgress> progress();
}

class ApiGamesRepository implements GamesRepository {
  ApiGamesRepository(this._client);

  final ApiClient _client;

  @override
  Future<List<Game>> games() async => (await _client.getJsonList('/api/v1/games'))
      .map((e) => Game.fromJson(e as Map<String, dynamic>))
      .toList();

  @override
  Future<Game> dailyChallenge() async {
    final json = await _client.getJson('/api/v1/games/daily');
    return Game.fromJson(json['game'] as Map<String, dynamic>);
  }

  @override
  Future<ScoreResult> submitScore(
    String code,
    double score,
    Map<String, dynamic> detail,
  ) async =>
      ScoreResult.fromJson(await _client.postJson(
        '/api/v1/games/$code/sessions',
        body: {'score': score, 'detail': detail},
      ));

  @override
  Future<Leaderboard> leaderboard(
    String code, {
    String scope = 'global',
    String? country,
  }) async =>
      Leaderboard.fromJson(
          await _client.getJson('/api/v1/games/$code/leaderboard', query: {
        'scope': scope,
        'country': ?country,
      }));

  @override
  Future<GameProgress> progress() async =>
      GameProgress.fromJson(await _client.getJson('/api/v1/games/me/progress'));
}

final gamesRepositoryProvider = Provider<GamesRepository>(
  (ref) => ApiGamesRepository(ref.watch(apiClientProvider)),
);
