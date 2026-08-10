import 'package:flutter/material.dart';

/// Date and time formatting.
///
/// The API stores and returns UTC. Every user-facing timestamp is converted to
/// the device's local zone exactly once, here — a screen that formats a UTC
/// value directly would silently show the wrong time to everyone outside UTC.
abstract final class Formatting {
  /// Local calendar date, e.g. "10/08/2026" in the device's locale.
  static String date(BuildContext context, DateTime utc) =>
      MaterialLocalizations.of(context).formatShortDate(utc.toLocal());

  /// Local time of day.
  static String time(BuildContext context, DateTime utc) =>
      MaterialLocalizations.of(context)
          .formatTimeOfDay(TimeOfDay.fromDateTime(utc.toLocal()));

  static String dateTime(BuildContext context, DateTime utc) =>
      '${date(context, utc)} · ${time(context, utc)}';

  /// Coarse relative age ("2h ago"). Uses the same instant in both zones, so
  /// the result is zone-independent by construction.
  static String relative(DateTime utc, {DateTime? now}) {
    final elapsed = (now ?? DateTime.now().toUtc()).difference(utc.toUtc());
    if (elapsed.isNegative) return 'soon';
    if (elapsed.inMinutes < 1) return 'just now';
    if (elapsed.inMinutes < 60) return '${elapsed.inMinutes}m ago';
    if (elapsed.inHours < 24) return '${elapsed.inHours}h ago';
    if (elapsed.inDays < 7) return '${elapsed.inDays}d ago';
    final weeks = elapsed.inDays ~/ 7;
    if (weeks < 5) return '${weeks}w ago';
    final months = elapsed.inDays ~/ 30;
    if (months < 12) return '${months}mo ago';
    return '${elapsed.inDays ~/ 365}y ago';
  }
}
