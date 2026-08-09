import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/network/api_client.dart';
import '../domain/models.dart';
import 'token_store.dart';

final apiClientProvider = Provider<ApiClient>((ref) => ApiClient());

final tokenStoreProvider = Provider<TokenStore>((ref) => const SecureTokenStore());

/// Catalogue reads (sports, athletes, competitions, home feed).
///
/// Abstract so tests (and any future offline mode) can substitute fakes; the
/// API implementation is the only one wired in production.
abstract class CatalogRepository {
  Future<List<Sport>> listSports({String? category});
  Future<Sport> getSport(String code);
  Future<Paged<Athlete>> listAthletes({String? sport, int page});
  Future<Paged<Competition>> listCompetitions({String? level, int page});
  Future<Competition> getCompetition(String slug);
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
  Future<Competition> getCompetition(String slug) async =>
      Competition.fromJson(await _client.getJson('/api/v1/competitions/$slug'));

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

/// Authentication against /api/v1/auth. The JWT is kept on the client and
/// persisted in secure storage so sessions survive restarts.
class AuthRepository {
  AuthRepository(this._client, this._tokens);

  final ApiClient _client;
  final TokenStore _tokens;

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
    final accessToken = token['access_token'] as String;
    _client.accessToken = accessToken;
    final user =
        UserAccount.fromJson(await _client.getJson('/api/v1/auth/me'));
    await _tokens.write(accessToken);
    return user;
  }

  /// Restore a persisted session; returns null when there is none or the
  /// stored token is no longer valid (in which case it is discarded).
  Future<UserAccount?> restoreSession() async {
    String? stored;
    try {
      stored = await _tokens.read();
    } catch (_) {
      return null; // secure storage unavailable — treat as signed out
    }
    if (stored == null) return null;
    _client.accessToken = stored;
    try {
      return UserAccount.fromJson(await _client.getJson('/api/v1/auth/me'));
    } on ApiException catch (e) {
      _client.accessToken = null;
      if (!e.isConnectionError) await _tokens.clear();
      return null;
    }
  }

  Future<void> logout() async {
    _client.accessToken = null;
    await _tokens.clear();
  }
}

final authRepositoryProvider = Provider<AuthRepository>(
  (ref) => AuthRepository(ref.watch(apiClientProvider), ref.watch(tokenStoreProvider)),
);
