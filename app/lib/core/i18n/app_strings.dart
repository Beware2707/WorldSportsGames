import 'package:flutter/widgets.dart';

/// Centralized user-facing strings.
///
/// Every string the user reads lives here rather than inline in widgets, so
/// translating the app is a matter of adding a subclass — not hunting through
/// the widget tree. English is the base; [lookup] falls back to it for any
/// locale that is not yet translated, so an untranslated locale renders
/// English rather than blank keys.
class AppStrings {
  const AppStrings();

  static const supportedLocales = [Locale('en')];

  static AppStrings of(BuildContext context) =>
      lookup(Localizations.maybeLocaleOf(context));

  static AppStrings lookup(Locale? locale) => switch (locale?.languageCode) {
        // Additional locales register here; unknown ones fall back to English.
        _ => const AppStrings(),
      };

  // Navigation
  String get home => 'Home';
  String get live => 'Live';
  String get sports => 'Sports';
  String get games => 'Games';
  String get profile => 'Profile';

  // Common states
  String get tryAgain => 'Try again';
  String get search => 'Search';
  String get signIn => 'Sign in';
  String get signOut => 'Sign out';
  String get notifications => 'Notifications';
  String get nothingLiveTitle => 'Nothing is live right now';
  String get nothingLiveMessage =>
      'Events appear here the moment genuine live coverage starts — never '
      'simulated.';
  String get liveStalePaused =>
      'Live updates are paused — reconnecting. The results below are the last '
      'known state, not live.';
  String get cannotReachServer =>
      'Cannot reach the server. Check your connection.';

  // Labels used in accessibility announcements
  String get liveBadgeLabel => 'Live now';
  String get lastKnownLabel => 'Last known result, not live';
  String estimateLabel(String confidence) =>
      'Automated estimate, confidence $confidence';

  String unreadNotifications(int count) =>
      count == 1 ? '1 unread notification' : '$count unread notifications';

  String medalCount(int count, String metal) =>
      count == 1 ? '1 $metal medal' : '$count $metal medals';

  String rankPosition(int rank) => 'Rank $rank';
}

/// Makes [AppStrings] available via `AppStrings.of(context)`.
class AppStringsScope extends InheritedWidget {
  const AppStringsScope({
    super.key,
    required this.strings,
    required super.child,
  });

  final AppStrings strings;

  static AppStrings of(BuildContext context) {
    final scope =
        context.dependOnInheritedWidgetOfExactType<AppStringsScope>();
    return scope?.strings ?? const AppStrings();
  }

  @override
  bool updateShouldNotify(AppStringsScope oldWidget) =>
      strings != oldWidget.strings;
}
