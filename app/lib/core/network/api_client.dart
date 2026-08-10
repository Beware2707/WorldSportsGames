import 'package:dio/dio.dart';

import '../config/app_config.dart';

/// Uniform exception surfaced to the UI layer for every network failure.
class ApiException implements Exception {
  const ApiException(this.message, {this.statusCode});

  final String message;
  final int? statusCode;

  bool get isConnectionError => statusCode == null;

  @override
  String toString() => message;
}

/// Thin wrapper around Dio: base URL, timeouts, auth header, error mapping.
class ApiClient {
  ApiClient({Dio? dio})
      : _dio = dio ??
            Dio(BaseOptions(
              baseUrl: AppConfig.apiBaseUrl,
              connectTimeout: const Duration(seconds: 8),
              receiveTimeout: const Duration(seconds: 12),
            ));

  final Dio _dio;
  String? _accessToken;

  set accessToken(String? token) => _accessToken = token;

  Future<Map<String, dynamic>> getJson(
    String path, {
    Map<String, dynamic>? query,
  }) async {
    try {
      final response = await _dio.get<Map<String, dynamic>>(
        path,
        queryParameters: query,
        options: _options(),
      );
      return response.data ?? const <String, dynamic>{};
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Future<List<dynamic>> getJsonList(
    String path, {
    Map<String, dynamic>? query,
  }) async {
    try {
      final response = await _dio.get<List<dynamic>>(
        path,
        queryParameters: query,
        options: _options(),
      );
      return response.data ?? const <dynamic>[];
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Future<Map<String, dynamic>> postJson(
    String path, {
    Object? body,
    Map<String, dynamic>? formData,
  }) async {
    try {
      final response = await _dio.post<Map<String, dynamic>>(
        path,
        data: formData != null ? FormData.fromMap(formData) : body,
        options: _options(),
      );
      return response.data ?? const <String, dynamic>{};
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Future<List<dynamic>> postJsonList(String path, {Object? body}) async {
    try {
      final response =
          await _dio.post<List<dynamic>>(path, data: body, options: _options());
      return response.data ?? const <dynamic>[];
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Future<List<dynamic>> putJsonList(String path, {Object? body}) async {
    try {
      final response =
          await _dio.put<List<dynamic>>(path, data: body, options: _options());
      return response.data ?? const <dynamic>[];
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Future<void> delete(String path) async {
    try {
      await _dio.delete<void>(path, options: _options());
    } on DioException catch (e) {
      throw _map(e);
    }
  }

  Options _options() => Options(headers: {
        if (_accessToken != null) 'Authorization': 'Bearer $_accessToken',
      });

  ApiException _map(DioException e) {
    final status = e.response?.statusCode;
    final data = e.response?.data;
    if (data is Map) {
      final detail = data['detail'];
      if (detail is String) {
        return ApiException(detail, statusCode: status);
      }
      // FastAPI validation errors: detail is a list of {loc, msg, type}.
      // Surface the actual constraint rather than a generic failure.
      if (detail is List && detail.isNotEmpty) {
        final messages = detail
            .whereType<Map>()
            .map((e) => e['msg'])
            .whereType<String>()
            .toList();
        if (messages.isNotEmpty) {
          return ApiException(messages.join('\n'), statusCode: status);
        }
      }
    }
    if (status != null) {
      return ApiException('Request failed ($status)', statusCode: status);
    }
    return const ApiException('Cannot reach the server. Check your connection.');
  }
}
