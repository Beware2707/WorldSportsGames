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

/// The LIVE badge animates forever, so pumpAndSettle never settles once a
/// live card is on screen; pump bounded frames instead.
Future<void> _settle(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
  await tester.pump(const Duration(milliseconds: 50));
}

void main() {
  testWidgets('honest empty state when nothing is live', (tester) async {
    final socket = FakeLiveSocket();
    await tester.pumpWidget(_wrap(FakeEventsRepository(), socket));
    await _settle(tester);

    expect(find.text('Nothing is live right now'), findsOneWidget);
    expect(find.text('LIVE'), findsNothing);
  });

  testWidgets('renders live events and applies WS diffs', (tester) async {
    final socket = FakeLiveSocket();
    final repo = FakeEventsRepository(live: [fakeLiveEvent]);
    await tester.pumpWidget(_wrap(repo, socket));
    await _settle(tester);

    // Before the socket confirms streaming, data is honestly "last known".
    expect(find.text('LAST KNOWN'), findsOneWidget);

    socket.controller.add(LiveSnapshot([fakeLiveEvent]));
    await _settle(tester);

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
    await _settle(tester);
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
    await _settle(tester);
    expect(find.text('Nothing is live right now'), findsOneWidget);
  });

  testWidgets('a dropped socket stops presenting data as LIVE', (tester) async {
    final socket = FakeLiveSocket();
    final repo = FakeEventsRepository(live: [fakeLiveEvent]);
    await tester.pumpWidget(_wrap(repo, socket));
    await _settle(tester);

    socket.controller.add(LiveSnapshot([fakeLiveEvent]));
    await _settle(tester);
    expect(find.text('LIVE'), findsOneWidget);

    // Server restart / network blip.
    socket.controller.add(const LiveDisconnected());
    await _settle(tester);

    // The event is still listed, but never as live — and the user is told.
    expect(find.text("Women's 100m — Round 1"), findsOneWidget);
    expect(find.text('LIVE'), findsNothing);
    expect(find.text('LAST KNOWN'), findsOneWidget);
    expect(find.textContaining('Live updates are paused'), findsOneWidget);
  });
}
