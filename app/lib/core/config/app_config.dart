import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';

/// Environment-driven app configuration.
///
/// Override at build time, e.g.
/// `flutter run --dart-define=API_BASE_URL=https://api.example.com`.
class AppConfig {
  const AppConfig._();

  static const String _definedBaseUrl = String.fromEnvironment('API_BASE_URL');

  /// Android emulators reach the host machine via 10.0.2.2; everything else
  /// defaults to localhost for development.
  static String get apiBaseUrl {
    if (_definedBaseUrl.isNotEmpty) return _definedBaseUrl;
    if (!kIsWeb && Platform.isAndroid) return 'http://10.0.2.2:8000';
    return 'http://127.0.0.1:8000';
  }
}
