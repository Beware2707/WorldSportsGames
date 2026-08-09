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

  Options _options() => Options(headers: {
        if (_accessToken != null) 'Authorization': 'Bearer $_accessToken',
      });

  ApiException _map(DioException e) {
    final status = e.response?.statusCode;
    final data = e.response?.data;
    if (data is Map && data['detail'] is String) {
      return ApiException(data['detail'] as String, statusCode: status);
    }
    if (status != null) {
      return ApiException('Request failed ($status)', statusCode: status);
    }
    return const ApiException('Cannot reach the server. Check your connection.');
  }
}
