import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/i18n/app_strings.dart';
import 'package:world_sports_games/core/i18n/formatting.dart';
import 'package:world_sports_games/core/widgets/common.dart';
import 'package:world_sports_games/core/widgets/insight_card.dart';
import 'package:world_sports_games/domain/insight_models.dart';

void main() {
  group('i18n scaffolding', () {
    test('an unsupported locale falls back to English, never to blanks', () {
      final english = AppStrings.lookup(const Locale('en'));
      final klingon = AppStrings.lookup(const Locale('tlh'));
      final none = AppStrings.lookup(null);

      expect(klingon.home, english.home);
      expect(none.home, english.home);
      expect(english.home, isNotEmpty);
    });

    test('plural forms are handled, not string-concatenated', () {
      const strings = AppStrings();
      expect(strings.unreadNotifications(1), '1 unread notification');
      expect(strings.unreadNotifications(4), '4 unread notifications');
      expect(strings.medalCount(1, 'gold'), '1 gold medal');
      expect(strings.medalCount(3, 'gold'), '3 gold medals');
    });

    testWidgets('strings resolve through context', (tester) async {
      late AppStrings resolved;
      await tester.pumpWidget(MaterialApp(
        home: Builder(builder: (context) {
          resolved = AppStrings.of(context);
          return const SizedBox.shrink();
        }),
      ));
      expect(resolved.live, 'Live');
    });
  });

  group('timezone handling', () {
    test('relative time is computed from UTC instants', () {
      final now = DateTime.utc(2026, 8, 10, 12, 0);
      expect(Formatting.relative(DateTime.utc(2026, 8, 10, 11, 30), now: now),
          '30m ago');
      expect(Formatting.relative(DateTime.utc(2026, 8, 10, 9, 0), now: now),
          '3h ago');
      expect(Formatting.relative(DateTime.utc(2026, 8, 7, 12, 0), now: now),
          '3d ago');
      // A future timestamp must not render as a negative age.
      expect(Formatting.relative(DateTime.utc(2026, 8, 11), now: now), 'soon');
    });

    test('a local-zone input gives the same answer as its UTC equivalent', () {
      final now = DateTime.utc(2026, 8, 10, 12, 0);
      final utcInstant = DateTime.utc(2026, 8, 10, 10, 0);
      expect(
        Formatting.relative(utcInstant.toLocal(), now: now),
        Formatting.relative(utcInstant, now: now),
        reason: 'relative age must not depend on the device zone',
      );
    });

    testWidgets('dates render in the device zone, not raw UTC', (tester) async {
      late String rendered;
      // 23:30 UTC — in any zone ahead of UTC this is the following day.
      final instant = DateTime.utc(2026, 8, 10, 23, 30);
      await tester.pumpWidget(MaterialApp(
        home: Builder(builder: (context) {
          rendered = Formatting.date(context, instant);
          return const SizedBox.shrink();
        }),
      ));

      final expected = instant.toLocal();
      expect(rendered, contains('${expected.day}'));
      expect(rendered, contains('${expected.year}'));
    });
  });

  group('accessibility', () {
    testWidgets('the LIVE badge is announced, not just coloured',
        (tester) async {
      final handle = tester.ensureSemantics();
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: LiveBadge())),
      );
      await tester.pump(const Duration(milliseconds: 50));

      // Colour and a pulsing dot mean nothing to a screen reader.
      expect(find.bySemanticsLabel('Live now'), findsOneWidget);
      handle.dispose();
    });

    testWidgets('an AI estimate exposes its label in the tree', (tester) async {
      await tester.pumpWidget(const MaterialApp(
        home: Scaffold(
          body: InsightCard(
            insight: AIInsight(
              kind: 'prediction',
              text: 'Form suggests an improvement.',
              provider: 'deterministic',
              isEstimate: true,
              disclaimer: 'This is an automated estimate.',
              basis: [],
              confidence: 'low',
            ),
          ),
        ),
      ));

      // The caveat is real text, so assistive tech reads it like any other.
      expect(find.text('ESTIMATE'), findsOneWidget);
      expect(find.text('This is an automated estimate.'), findsOneWidget);
    });

    testWidgets('interactive controls meet the minimum tap target',
        (tester) async {
      final handle = tester.ensureSemantics();
      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: ErrorState(message: 'Offline', onRetry: () {}),
        ),
      ));

      await expectLater(tester, meetsGuideline(androidTapTargetGuideline));
      await expectLater(tester, meetsGuideline(iOSTapTargetGuideline));
      handle.dispose();
    });

    testWidgets('empty and error states have readable labels', (tester) async {
      final handle = tester.ensureSemantics();
      await tester.pumpWidget(const MaterialApp(
        home: Scaffold(
          body: EmptyState(
            icon: Icons.search_off_rounded,
            title: 'No matches',
            message: 'Try a broader term.',
          ),
        ),
      ));

      await expectLater(tester, meetsGuideline(textContrastGuideline));
      handle.dispose();
    });
  });
}
