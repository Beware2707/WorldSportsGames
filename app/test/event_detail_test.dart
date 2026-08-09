import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/widgets/result_tile.dart';
import 'package:world_sports_games/data/live_repository.dart';
import 'package:world_sports_games/domain/event_models.dart';
import 'package:world_sports_games/domain/models.dart';
import 'package:world_sports_games/features/events/event_detail_screen.dart';

import 'fakes.dart';

void main() {
  testWidgets('event detail renders ranked results and DNF chip',
      (tester) async {
    await tester.pumpWidget(ProviderScope(
      overrides: [
        eventsRepositoryProvider.overrideWithValue(FakeEventsRepository()),
      ],
      child: const MaterialApp(home: EventDetailScreen(eventId: 1)),
    ));
    await tester.pumpAndSettle();

    expect(find.text("Women's 100m Final"), findsWidgets);
    expect(find.text('Zellie Dunbar'), findsOneWidget);
    expect(find.text('10.71'), findsOneWidget);
    expect(find.text('DNF'), findsOneWidget);
  });

  testWidgets('ResultTile renders points kind with medal badge',
      (tester) async {
    const result = EventResult(
      athlete: Athlete(id: 3, slug: 'wei-lin-zhao', fullName: 'Wei-Lin Zhao'),
      position: 1,
      status: 'ok',
      valueKind: 'points',
      valueText: '57.566',
    );
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(body: ResultTile(result: result)),
    ));

    expect(find.text('57.566'), findsOneWidget);
    expect(find.text('1'), findsOneWidget);
  });

  test('LiveUpdateFrame parses payload', () {
    final frame = LiveUpdateFrame.fromJson(const {
      'event_id': 9,
      'seq': 2,
      'kind': 'results',
      'payload': {'standings': [], 'source': 'dev-sim'},
    });
    expect(frame.eventId, 9);
    expect(frame.payload['source'], 'dev-sim');
  });
}
