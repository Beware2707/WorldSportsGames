import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:world_sports_games/data/personalization_repository.dart';
import 'package:world_sports_games/data/repositories.dart';
import 'package:world_sports_games/domain/models.dart';
import 'package:world_sports_games/domain/personalization_models.dart';
import 'package:world_sports_games/features/onboarding/onboarding_screen.dart';
import 'package:world_sports_games/features/profile/profile_screen.dart';

import 'fakes.dart';

const _user = UserAccount(id: 1, email: 'a@example.com', displayName: 'A');

class _SignedInUser extends CurrentUserNotifier {
  @override
  UserAccount? build() => _user;
}

Widget _wrap(
  FakePersonalizationRepository personalization,
  FakeCatalogRepository catalog,
) {
  final router = GoRouter(
    initialLocation: '/onboarding',
    routes: [
      GoRoute(path: '/onboarding', builder: (_, _) => const OnboardingScreen()),
      GoRoute(
          path: '/home',
          builder: (_, _) => const Scaffold(body: Text('HOME'))),
    ],
  );
  return ProviderScope(
    overrides: [
      personalizationRepositoryProvider.overrideWithValue(personalization),
      catalogRepositoryProvider.overrideWithValue(catalog),
      currentUserProvider.overrideWith(() => _SignedInUser()),
    ],
    child: MaterialApp.router(routerConfig: router),
  );
}

void main() {
  testWidgets('Skip leaves existing follows untouched', (tester) async {
    // A returning user who already follows things.
    final personalization = FakePersonalizationRepository()
      ..stored.addAll(const [
        Follow(kind: FollowKind.sport, entityId: 1, name: 'Athletics'),
        Follow(kind: FollowKind.athlete, entityId: 9, name: 'Amara Okafor'),
      ]);

    await tester.pumpWidget(_wrap(personalization, FakeCatalogRepository()));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Skip'));
    await tester.pumpAndSettle();

    expect(
      personalization.stored.length,
      2,
      reason: 'skipping must never delete follows',
    );
  });

  testWidgets('existing follows are pre-selected so Finish preserves them',
      (tester) async {
    final personalization = FakePersonalizationRepository()
      ..stored.addAll(const [
        Follow(kind: FollowKind.sport, entityId: 1, name: 'Athletics'),
        Follow(kind: FollowKind.sport, entityId: 2, name: 'Curling'),
      ]);

    await tester.pumpWidget(_wrap(personalization, FakeCatalogRepository()));
    await tester.pumpAndSettle();

    // Both existing follows show as already selected.
    expect(find.text('2 selected'), findsOneWidget);

    // Advance to the last step and submit without changing anything.
    for (var i = 0; i < 3; i++) {
      await tester.tap(find.text('Next'));
      await tester.pumpAndSettle();
    }
    await tester.tap(find.text('Finish'));
    await tester.pumpAndSettle();

    expect(
      personalization.stored.map((f) => f.entityId).toSet(),
      {1, 2},
      reason: 'finishing unchanged must be a no-op, not a wipe',
    );
  });

  testWidgets('deselecting an existing follow does remove it', (tester) async {
    final personalization = FakePersonalizationRepository()
      ..stored.addAll(const [
        Follow(kind: FollowKind.sport, entityId: 1, name: 'Athletics'),
        Follow(kind: FollowKind.sport, entityId: 2, name: 'Curling'),
      ]);

    await tester.pumpWidget(_wrap(personalization, FakeCatalogRepository()));
    await tester.pumpAndSettle();

    await tester.tap(find.widgetWithText(FilterChip, 'Curling'));
    await tester.pumpAndSettle();
    expect(find.text('1 selected'), findsOneWidget);

    for (var i = 0; i < 3; i++) {
      await tester.tap(find.text('Next'));
      await tester.pumpAndSettle();
    }
    await tester.tap(find.text('Finish'));
    await tester.pumpAndSettle();

    expect(personalization.stored.map((f) => f.entityId).toSet(), {1});
  });
}
