import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/data/personalization_repository.dart';
import 'package:world_sports_games/domain/personalization_models.dart';
import 'package:world_sports_games/features/search/search_screen.dart';

import 'fakes.dart';

Widget _wrap(FakePersonalizationRepository repo) => ProviderScope(
      overrides: [personalizationRepositoryProvider.overrideWithValue(repo)],
      child: const MaterialApp(home: SearchScreen()),
    );

void main() {
  testWidgets('empty query shows guidance, not a spinner or results',
      (tester) async {
    await tester.pumpWidget(_wrap(FakePersonalizationRepository()));
    await tester.pumpAndSettle();

    expect(find.text('Search the platform'), findsOneWidget);
  });

  testWidgets('typing debounces then renders categorized suggestions',
      (tester) async {
    final repo = FakePersonalizationRepository()
      ..suggestions = const [
        SearchSuggestion(
            kind: 'sport', id: 2, label: 'Athletics',
            sublabel: 'summer', slug: 'athletics'),
        SearchSuggestion(
            kind: 'athlete', id: 9, label: 'Amara Okafor',
            sublabel: 'United States', slug: 'amara-okafor'),
      ];
    await tester.pumpWidget(_wrap(repo));
    await tester.pumpAndSettle();

    await tester.enterText(find.byType(TextField), 'ath');
    await tester.pump(); // debounce still pending
    expect(find.text('Athletics'), findsNothing);

    await tester.pump(const Duration(milliseconds: 300));
    await tester.pumpAndSettle();

    expect(find.text('Athletics'), findsOneWidget);
    expect(find.text('Amara Okafor'), findsOneWidget);
    expect(find.textContaining('Sport'), findsOneWidget);
    expect(find.textContaining('Athlete'), findsOneWidget);
  });

  testWidgets('no matches shows an honest empty state naming the query',
      (tester) async {
    await tester.pumpWidget(_wrap(FakePersonalizationRepository()));
    await tester.pumpAndSettle();

    await tester.enterText(find.byType(TextField), 'zzzz');
    await tester.pump(const Duration(milliseconds: 300));
    await tester.pumpAndSettle();

    expect(find.text('No matches for "zzzz"'), findsOneWidget);
  });
}
