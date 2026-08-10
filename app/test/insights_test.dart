import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/widgets/insight_card.dart';
import 'package:world_sports_games/domain/insight_models.dart';

void main() {
  group('AIInsight parsing', () {
    test('parses a labelled estimate', () {
      final insight = AIInsight.fromJson(const {
        'kind': 'performance_trend',
        'text': 'The trend appears improving.',
        'provider': 'deterministic',
        'is_estimate': true,
        'disclaimer': 'This is an automated estimate.',
        'basis': ['4 recorded results'],
        'confidence': 'low',
      });
      expect(insight.isEstimate, isTrue);
      expect(insight.disclaimer, 'This is an automated estimate.');
      expect(insight.basis.single, '4 recorded results');
    });

    test('a missing or blank disclaimer is replaced, never left empty', () {
      // Defence in depth: the server enforces this, but a client that renders
      // bare generated text would be the actual harm.
      final blank = AIInsight.fromJson(const {
        'kind': 'prediction',
        'text': 'Anything could happen.',
        'provider': 'x',
        'is_estimate': true,
        'disclaimer': '   ',
        'basis': [],
        'confidence': 'low',
      });
      expect(blank.disclaimer.trim(), isNotEmpty);

      final missing = AIInsight.fromJson(const {
        'kind': 'prediction',
        'text': 'Anything could happen.',
        'provider': 'x',
        'is_estimate': true,
        'basis': [],
        'confidence': 'low',
      });
      expect(missing.disclaimer.trim(), isNotEmpty);
    });
  });

  group('InsightCard', () {
    Future<void> pump(WidgetTester tester, AIInsight insight) =>
        tester.pumpWidget(MaterialApp(
          home: Scaffold(body: InsightCard(insight: insight)),
        ));

    testWidgets('an estimate is visibly labelled as one', (tester) async {
      await pump(
        tester,
        const AIInsight(
          kind: 'performance_trend',
          text: 'Form looks to be improving.',
          provider: 'deterministic',
          isEstimate: true,
          disclaimer: 'This is an automated estimate based on past results.',
          basis: ['5 recorded results'],
          confidence: 'medium',
        ),
      );

      expect(find.text('ESTIMATE'), findsOneWidget);
      expect(find.textContaining('automated estimate'), findsOneWidget);
      expect(find.textContaining('Form looks to be improving'), findsOneWidget);
    });

    testWidgets('a derived summary is labelled differently but still caveated',
        (tester) async {
      await pump(
        tester,
        const AIInsight(
          kind: 'athlete_summary',
          text: 'Zellie Dunbar competes for Jamaica.',
          provider: 'deterministic',
          isEstimate: false,
          disclaimer: 'Automatically generated from recorded results.',
          basis: ['recorded medals'],
          confidence: 'high',
        ),
      );

      expect(find.text('AUTO-GENERATED'), findsOneWidget);
      expect(find.text('ESTIMATE'), findsNothing);
      expect(find.textContaining('Automatically generated'), findsOneWidget);
    });

    testWidgets('the disclaimer is always rendered', (tester) async {
      for (final estimate in [true, false]) {
        await pump(
          tester,
          AIInsight(
            kind: 'x',
            text: 'Body text',
            provider: 'p',
            isEstimate: estimate,
            disclaimer: 'Always visible caveat',
            basis: const [],
            confidence: 'unknown',
          ),
        );
        expect(find.text('Always visible caveat'), findsOneWidget);
      }
    });
  });

  group('notifications', () {
    test('unread state and route extraction', () {
      final unread = AppNotification.fromJson(const {
        'id': 1,
        'kind': 'medal_result',
        'title': 'Gold for Zellie Dunbar',
        'body': 'She took gold.',
        'payload': {'route': '/athletes/zellie-dunbar'},
        'read_at': null,
        'created_at': '2026-08-10T10:00:00Z',
      });
      expect(unread.isUnread, isTrue);
      expect(unread.route, '/athletes/zellie-dunbar');

      final read = AppNotification.fromJson(const {
        'id': 2,
        'kind': 'breaking_news',
        'title': 'News',
        'body': 'Something',
        'payload': {},
        'read_at': '2026-08-10T11:00:00Z',
        'created_at': '2026-08-10T10:00:00Z',
      });
      expect(read.isUnread, isFalse);
      expect(read.route, isNull);
    });
  });

  group('media', () {
    test('duration formats and publisher link is preserved', () {
      final item = MediaItem.fromJson(const {
        'id': 1,
        'kind': 'video',
        'title': 'Race highlights',
        'url': 'https://publisher.example/watch/1',
        'source': 'publisher',
        'published_at': '2026-08-01T12:00:00Z',
        'duration_seconds': 125,
        'summary': null,
        'thumbnail_url': null,
        'sports': ['Athletics'],
        'athletes': [],
      });
      expect(item.durationLabel, '2:05');
      // The platform links out; it never claims to host the media.
      expect(item.url, 'https://publisher.example/watch/1');
      expect(item.source, 'publisher');
    });
  });
}
