import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/data/repositories.dart';
import 'package:world_sports_games/features/home/home_screen.dart';

import 'fakes.dart';

Widget _wrap(CatalogRepository repo) => ProviderScope(
      overrides: [catalogRepositoryProvider.overrideWithValue(repo)],
      child: const MaterialApp(home: HomeScreen()),
    );

void main() {
  testWidgets('renders sections and an honest empty live state',
      (tester) async {
    await tester.pumpWidget(_wrap(FakeCatalogRepository()));
    await tester.pumpAndSettle();

    expect(find.text('Live Now'), findsOneWidget);
    expect(find.textContaining('No events are live right now'), findsOneWidget);
    // No LIVE badge may appear when nothing is genuinely live.
    expect(find.text('LIVE'), findsNothing);

    expect(find.text('Up Next'), findsOneWidget);
    expect(find.text('Olympic Games'), findsOneWidget);
    expect(find.text('Amara Okafor'), findsOneWidget);
  });

  testWidgets('shows error state with retry on failure', (tester) async {
    await tester.pumpWidget(_wrap(
        FakeCatalogRepository(failWith: const ApiException('Server down'))));
    await tester.pumpAndSettle();

    expect(find.text('Server down'), findsOneWidget);
    expect(find.text('Try again'), findsOneWidget);
  });
}
