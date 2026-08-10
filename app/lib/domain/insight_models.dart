/// AI insights, media and notifications.
library;

/// A generated insight.
///
/// [disclaimer] is non-nullable by design: the UI has no code path that can
/// render generated text without its caveat.
class AIInsight {
  const AIInsight({
    required this.kind,
    required this.text,
    required this.provider,
    required this.isEstimate,
    required this.disclaimer,
    required this.basis,
    required this.confidence,
  });

  final String kind;
  final String text;
  final String provider;
  final bool isEstimate;
  final String disclaimer;
  final List<String> basis;
  final String confidence;

  factory AIInsight.fromJson(Map<String, dynamic> json) {
    final disclaimer = (json['disclaimer'] as String?)?.trim() ?? '';
    return AIInsight(
      kind: json['kind'] as String,
      text: json['text'] as String,
      provider: json['provider'] as String,
      isEstimate: json['is_estimate'] as bool,
      // A server that somehow omits the caveat must not produce bare text.
      disclaimer: disclaimer.isEmpty
          ? 'Automatically generated — verify against official sources.'
          : disclaimer,
      basis: (json['basis'] as List? ?? const []).cast<String>(),
      confidence: json['confidence'] as String? ?? 'unknown',
    );
  }
}

class MediaItem {
  const MediaItem({
    required this.id,
    required this.kind,
    required this.title,
    required this.url,
    required this.source,
    required this.publishedAt,
    this.summary,
    this.thumbnailUrl,
    this.durationSeconds,
    this.sports = const [],
    this.athletes = const [],
  });

  final int id;
  final String kind;
  final String title;
  final String url;
  final String source;
  final DateTime publishedAt;
  final String? summary;
  final String? thumbnailUrl;
  final int? durationSeconds;
  final List<String> sports;
  final List<String> athletes;

  String? get durationLabel {
    final seconds = durationSeconds;
    if (seconds == null) return null;
    final minutes = seconds ~/ 60;
    final remainder = seconds % 60;
    return '$minutes:${remainder.toString().padLeft(2, '0')}';
  }

  factory MediaItem.fromJson(Map<String, dynamic> json) => MediaItem(
        id: json['id'] as int,
        kind: json['kind'] as String,
        title: json['title'] as String,
        url: json['url'] as String,
        source: json['source'] as String,
        publishedAt: DateTime.parse(json['published_at'] as String),
        summary: json['summary'] as String?,
        thumbnailUrl: json['thumbnail_url'] as String?,
        durationSeconds: json['duration_seconds'] as int?,
        sports: (json['sports'] as List? ?? const []).cast<String>(),
        athletes: (json['athletes'] as List? ?? const []).cast<String>(),
      );
}

class AppNotification {
  const AppNotification({
    required this.id,
    required this.kind,
    required this.title,
    required this.body,
    required this.payload,
    required this.createdAt,
    this.readAt,
  });

  final int id;
  final String kind;
  final String title;
  final String body;
  final Map<String, dynamic> payload;
  final DateTime createdAt;
  final DateTime? readAt;

  bool get isUnread => readAt == null;
  String? get route => payload['route'] as String?;

  factory AppNotification.fromJson(Map<String, dynamic> json) => AppNotification(
        id: json['id'] as int,
        kind: json['kind'] as String,
        title: json['title'] as String,
        body: json['body'] as String,
        payload: (json['payload'] as Map?)?.cast<String, dynamic>() ?? const {},
        createdAt: DateTime.parse(json['created_at'] as String),
        readAt: json['read_at'] == null
            ? null
            : DateTime.parse(json['read_at'] as String),
      );
}
