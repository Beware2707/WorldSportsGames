import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/data/repositories.dart';

import 'fakes.dart';

/// Dio with a stubbed adapter so AuthRepository can be exercised end-to-end
/// without a server.
({ApiClient client, _StubAdapter adapter}) _client() {
  final dio = Dio(BaseOptions(baseUrl: 'http://test'));
  final adapter = _StubAdapter();
  dio.httpClientAdapter = adapter;
  return (client: ApiClient(dio: dio), adapter: adapter);
}

class _StubAdapter implements HttpClientAdapter {
  int status = 200;
  Object body = const {
    'id': 1,
    'email': 'a@example.com',
    'display_name': 'A',
    'created_at': '2026-01-01T00:00:00Z',
  };
  bool throwConnectionError = false;

  @override
  Future<ResponseBody> fetch(RequestOptions options, Stream<List<int>>? _,
      Future<void>? _) async {
    if (throwConnectionError) {
      throw DioException.connectionError(
          requestOptions: options, reason: 'offline');
    }
    return ResponseBody.fromString(
      _encode(body),
      status,
      headers: {
        Headers.contentTypeHeader: [Headers.jsonContentType]
      },
    );
  }

  String _encode(Object value) =>
      const JsonEncoderShim().convert(value);

  @override
  void close({bool force = false}) {}
}

class JsonEncoderShim {
  const JsonEncoderShim();
  String convert(Object value) => _jsonEncode(value);
}

String _jsonEncode(Object? value) {
  if (value is Map) {
    return '{${value.entries.map((e) => '"${e.key}":${_jsonEncode(e.value)}').join(',')}}';
  }
  if (value is List) return '[${value.map(_jsonEncode).join(',')}]';
  if (value is String) return '"$value"';
  return '$value';
}

void main() {
  test('restoreSession returns null and keeps nothing when no token stored',
      () async {
    final (client: client, adapter: _) = _client();
    final store = FakeTokenStore();
    final repo = AuthRepository(client, store);

    expect(await repo.restoreSession(), isNull);
    expect(store.token, isNull);
  });

  test('restoreSession restores the user from a stored token', () async {
    final (client: client, adapter: _) = _client();
    final store = FakeTokenStore('stored-token');
    final repo = AuthRepository(client, store);

    final user = await repo.restoreSession();
    expect(user?.email, 'a@example.com');
    expect(store.token, 'stored-token', reason: 'valid token must be kept');
  });

  test('restoreSession DISCARDS a rejected token', () async {
    final (client: client, adapter: adapter) = _client();
    adapter.status = 401;
    adapter.body = const {'detail': 'Invalid or expired credentials'};
    final store = FakeTokenStore('revoked-token');
    final repo = AuthRepository(client, store);

    expect(await repo.restoreSession(), isNull);
    expect(store.token, isNull, reason: '401 means the token is dead');
  });

  test('restoreSession KEEPS the token when merely offline', () async {
    final (client: client, adapter: adapter) = _client();
    adapter.throwConnectionError = true;
    final store = FakeTokenStore('good-token');
    final repo = AuthRepository(client, store);

    expect(await repo.restoreSession(), isNull);
    expect(store.token, 'good-token',
        reason: 'a network blip must not sign the user out permanently');
  });

  test('restoreSession treats unreadable secure storage as signed out',
      () async {
    final (client: client, adapter: _) = _client();
    final store = FakeTokenStore('x')..readThrows = true;
    final repo = AuthRepository(client, store);

    expect(await repo.restoreSession(), isNull);
  });

  test('logout clears the persisted token', () async {
    final (client: client, adapter: _) = _client();
    final store = FakeTokenStore('token');
    await AuthRepository(client, store).logout();
    expect(store.token, isNull);
  });
}
