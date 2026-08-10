import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/competitive_models.dart';
import '../domain/models.dart';
import 'repositories.dart';

/// Rankings, records, medals and profile reads.
abstract class CompetitiveRepository {
  Future<RankingLadder> rankings({
    String scope,
    String methodology,
    String? discipline,
  });
  Future<List<SportRecord>> records({String? kind, String? discipline, String? gender});
  Future<MedalTable> medalTable({int? editionId});
  Future<List<Edition>> medalEditions();
  Future<CountryProfile> countryProfile(String iso3);
  Future<AthleteProfile> athleteProfile(String slug);
}

class ApiCompetitiveRepository implements CompetitiveRepository {
  ApiCompetitiveRepository(this._client);

  final ApiClient _client;

  @override
  Future<RankingLadder> rankings({
    String scope = 'athlete',
    String methodology = 'world_ranking',
    String? discipline,
  }) async =>
      RankingLadder.fromJson(await _client.getJson('/api/v1/rankings', query: {
        'scope': scope,
        'methodology': methodology,
        'discipline': ?discipline,
      }));

  @override
  Future<List<SportRecord>> records({
    String? kind,
    String? discipline,
    String? gender,
  }) async =>
      (await _client.getJsonList('/api/v1/records', query: {
        'kind': ?kind,
        'discipline': ?discipline,
        'gender': ?gender,
      }))
          .map((e) => SportRecord.fromJson(e as Map<String, dynamic>))
          .toList();

  @override
  Future<MedalTable> medalTable({int? editionId}) async =>
      MedalTable.fromJson(await _client.getJson('/api/v1/medals', query: {
        'edition_id': ?editionId,
      }));

  @override
  Future<List<Edition>> medalEditions() async =>
      (await _client.getJsonList('/api/v1/medals/editions'))
          .map((e) => Edition.fromJson(e as Map<String, dynamic>))
          .toList();

  @override
  Future<CountryProfile> countryProfile(String iso3) async =>
      CountryProfile.fromJson(await _client.getJson('/api/v1/countries/$iso3'));

  @override
  Future<AthleteProfile> athleteProfile(String slug) async => AthleteProfile.fromJson(
      await _client.getJson('/api/v1/athletes/$slug/profile'));
}

final competitiveRepositoryProvider = Provider<CompetitiveRepository>(
  (ref) => ApiCompetitiveRepository(ref.watch(apiClientProvider)),
);
