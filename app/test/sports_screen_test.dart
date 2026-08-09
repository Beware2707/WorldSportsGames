import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/data/repositories.dart';
import 'package:world_sports_games/features/sports/sports_screen.dart';

import 'fakes.dart';

Widget _wrap(CatalogRepository repo) => ProviderScope(
      overrides: [catalogRepositoryProvider.overrideWithValue(repo)],
      child: const MaterialApp(home: SportsScreen()),
    );

void main() {
  testWidgets('lists sports and filters by category', (tester) async {
    await tester.pumpWidget(_wrap(FakeCatalogRepository()));
    await tester.pumpAndSettle();

    expect(find.text('Athletics'), findsOneWidget);
    expect(find.text('Curling'), findsOneWidget);

    await tester.tap(find.text('Winter'));
    await tester.pumpAndSettle();

    expect(find.text('Curling'), findsOneWidget);
    expect(find.text('Athletics'), findsNothing);
  });
}
