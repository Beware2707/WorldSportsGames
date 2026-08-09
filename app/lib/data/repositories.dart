import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/models.dart';

final apiClientProvider = Provider<ApiClient>((ref) => ApiClient());

/// Catalogue reads (sports, athletes, competitions, home feed).
///
/// Abstract so tests (and any future offline mode) can substitute fakes; the
/// API implementation is the only one wired in production.
abstract class CatalogRepository {
  Future<List<Sport>> listSports({String? category});
  Future<Sport> getSport(String code);
  Future<Paged<Athlete>> listAthletes({String? sport, int page});
  Future<Paged<Competition>> listCompetitions({String? level, int page});
  Future<List<HomeSection>> homeFeed();
}

class ApiCatalogRepository implements CatalogRepository {
  ApiCatalogRepository(this._client);

  final ApiClient _client;

  @override
  Future<List<Sport>> listSports({String? category}) async {
    final json = await _client.getJson('/api/v1/sports', query: {
      'size': 100,
      'category': ?category,
    });
    return Paged.fromJson(json, Sport.fromJson).items;
  }

  @override
  Future<Sport> getSport(String code) async =>
      Sport.fromJson(await _client.getJson('/api/v1/sports/$code'));

  @override
  Future<Paged<Athlete>> listAthletes({String? sport, int page = 1}) async {
    final json = await _client.getJson('/api/v1/athletes', query: {
      'page': page,
      'sport': ?sport,
    });
    return Paged.fromJson(json, Athlete.fromJson);
  }

  @override
  Future<Paged<Competition>> listCompetitions({String? level, int page = 1}) async {
    final json = await _client.getJson('/api/v1/competitions', query: {
      'page': page,
      'level': ?level,
    });
    return Paged.fromJson(json, Competition.fromJson);
  }

  @override
  Future<List<HomeSection>> homeFeed() async {
    final json = await _client.getJson('/api/v1/home');
    return (json['sections'] as List)
        .map((e) => HomeSection.fromJson(e as Map<String, dynamic>))
        .toList();
  }
}

final catalogRepositoryProvider = Provider<CatalogRepository>(
  (ref) => ApiCatalogRepository(ref.watch(apiClientProvider)),
);

/// Authentication against /api/v1/auth. Token lives in memory for Sprint 1;
/// secure persistent storage arrives with the profile work in Sprint 3.
class AuthRepository {
  AuthRepository(this._client);

  final ApiClient _client;

  Future<UserAccount> register({
    required String email,
    required String password,
    required String displayName,
  }) async =>
      UserAccount.fromJson(await _client.postJson('/api/v1/auth/register', body: {
        'email': email,
        'password': password,
        'display_name': displayName,
      }));

  Future<UserAccount> login({required String email, required String password}) async {
    final token = await _client.postJson('/api/v1/auth/login', formData: {
      'username': email,
      'password': password,
    });
    _client.accessToken = token['access_token'] as String;
    return UserAccount.fromJson(await _client.getJson('/api/v1/auth/me'));
  }

  void logout() => _client.accessToken = null;
}

final authRepositoryProvider = Provider<AuthRepository>(
  (ref) => AuthRepository(ref.watch(apiClientProvider)),
);
