/// Follows, preferences and search-suggestion models.
library;

/// The kinds of entity a user can follow. Mirrors the backend's
/// FAVORITE_ENTITY_TYPES; the wire format is the snake_case `name`.
enum FollowKind {
  sport,
  discipline,
  athlete,
  country,
  competition;

  static FollowKind? fromWire(String value) {
    for (final kind in FollowKind.values) {
      if (kind.name == value) return kind;
    }
    return null; // unknown kind from a newer server — ignore, don't crash
  }
}

class Follow {
  const Follow({
    required this.kind,
    required this.entityId,
    this.name,
    this.subtitle,
    this.slug,
  });

  final FollowKind kind;
  final int entityId;
  final String? name;
  final String? subtitle;
  final String? slug;

  /// Null when the server sends a kind this client version doesn't know.
  static Follow? fromJson(Map<String, dynamic> json) {
    final kind = FollowKind.fromWire(json['entity_type'] as String);
    if (kind == null) return null;
    return Follow(
      kind: kind,
      entityId: json['entity_id'] as int,
      name: json['name'] as String?,
      subtitle: json['subtitle'] as String?,
      slug: json['slug'] as String?,
    );
  }

  Map<String, dynamic> toJson() =>
      {'entity_type': kind.name, 'entity_id': entityId};
}

class OnboardingState {
  const OnboardingState({required this.completed, required this.followCount});

  final bool completed;
  final int followCount;

  factory OnboardingState.fromJson(Map<String, dynamic> json) => OnboardingState(
        completed: json['completed'] as bool,
        followCount: json['follow_count'] as int,
      );
}

class NotificationSetting {
  const NotificationSetting({required this.kind, required this.enabled});

  final String kind;
  final bool enabled;

  factory NotificationSetting.fromJson(Map<String, dynamic> json) =>
      NotificationSetting(
        kind: json['kind'] as String,
        enabled: json['enabled'] as bool,
      );

  Map<String, dynamic> toJson() => {'kind': kind, 'enabled': enabled};

  NotificationSetting copyWith({bool? enabled}) =>
      NotificationSetting(kind: kind, enabled: enabled ?? this.enabled);

  /// Human label; falls back to a de-snake-cased kind so a server-side
  /// addition still renders sensibly before the client knows about it.
  String get label => switch (kind) {
        'athlete_event_start' => 'Athlete starts an event',
        'competition_start' => 'Competition starts',
        'live_result' => 'Live results',
        'medal_result' => 'Medal results',
        'record_broken' => 'Records broken',
        'event_reminder' => 'Event reminders',
        'followed_athlete_result' => 'Results for athletes you follow',
        'followed_country_event' => 'Events for countries you follow',
        'breaking_news' => 'Breaking news',
        _ => kind.replaceAll('_', ' '),
      };
}

class SearchSuggestion {
  const SearchSuggestion({
    required this.kind,
    required this.id,
    required this.label,
    this.sublabel,
    this.slug,
  });

  final String kind; // sport | athlete | competition | country
  final int id;
  final String label;
  final String? sublabel;
  final String? slug;

  factory SearchSuggestion.fromJson(Map<String, dynamic> json) => SearchSuggestion(
        kind: json['kind'] as String,
        id: json['id'] as int,
        label: json['label'] as String,
        sublabel: json['sublabel'] as String?,
        slug: json['slug'] as String?,
      );
}
