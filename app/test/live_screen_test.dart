import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/data/live_repository.dart';
import 'package:world_sports_games/domain/event_models.dart';
import 'package:world_sports_games/features/live/live_screen.dart';

import 'fakes.dart';

Widget _wrap(FakeEventsRepository repo, FakeLiveSocket socket) => ProviderScope(
      overrides: [
        eventsRepositoryProvider.overrideWithValue(repo),
        liveSocketProvider.overrideWithValue(socket),
      ],
      child: const MaterialApp(home: LiveScreen()),
    );

void main() {
  testWidgets('honest empty state when nothing is live', (tester) async {
    final socket = FakeLiveSocket();
    await tester.pumpWidget(_wrap(FakeEventsRepository(), socket));
    await tester.pumpAndSettle();

    expect(find.text('Nothing is live right now'), findsOneWidget);
    expect(find.text('LIVE'), findsNothing);
  });

  testWidgets('renders live events and applies WS diffs', (tester) async {
    final socket = FakeLiveSocket();
    final repo = FakeEventsRepository(live: [fakeLiveEvent]);
    await tester.pumpWidget(_wrap(repo, socket));
    // The LIVE badge pulses forever, so pumpAndSettle would never settle;
    // pump bounded frames instead.
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    expect(find.text("Women's 100m — Round 1"), findsOneWidget);
    expect(find.text('LIVE'), findsOneWidget);
    expect(find.textContaining('update #3'), findsOneWidget);

    // A progress diff bumps the sequence counter without a refetch.
    socket.controller.add(const LiveDiff(LiveUpdateFrame(
      eventId: 42,
      seq: 7,
      kind: 'progress',
      payload: {'phase': 'heat'},
    )));
    await tester.pump(const Duration(milliseconds: 50));
    await tester.pump(const Duration(milliseconds: 50));
    expect(find.textContaining('update #7'), findsOneWidget);

    // A status diff triggers a REST refetch; when the event has finished the
    // list honestly empties.
    repo.live = [];
    socket.controller.add(const LiveDiff(LiveUpdateFrame(
      eventId: 42,
      seq: 8,
      kind: 'status',
      payload: {'status': 'completed'},
    )));
    await tester.pump(const Duration(milliseconds: 50));
    await tester.pump(const Duration(milliseconds: 50));
    expect(find.text('Nothing is live right now'), findsOneWidget);
  });
}
