import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/data/personalization_repository.dart';
import 'package:world_sports_games/domain/models.dart';
import 'package:world_sports_games/domain/personalization_models.dart';
import 'package:world_sports_games/features/follows/follow_button.dart';
import 'package:world_sports_games/features/profile/profile_screen.dart';

import 'fakes.dart';

const _user = UserAccount(id: 1, email: 'a@example.com', displayName: 'A');

Widget _wrap(
  FakePersonalizationRepository repo, {
  bool signedIn = true,
}) =>
    ProviderScope(
      overrides: [
        personalizationRepositoryProvider.overrideWithValue(repo),
        if (signedIn)
          currentUserProvider.overrideWith(() => _SignedInUser()),
      ],
      child: const MaterialApp(
        home: Scaffold(
          body: FollowButton(
              kind: FollowKind.sport, entityId: 7, name: 'Athletics'),
        ),
      ),
    );

class _SignedInUser extends CurrentUserNotifier {
  @override
  UserAccount? build() => _user;
}

void main() {
  testWidgets('follow then unfollow round-trips through the repository',
      (tester) async {
    final repo = FakePersonalizationRepository();
    await tester.pumpWidget(_wrap(repo));
    await tester.pumpAndSettle();

    expect(find.text('Follow'), findsOneWidget);

    await tester.tap(find.text('Follow'));
    await tester.pumpAndSettle();

    expect(find.text('Following'), findsOneWidget);
    expect(repo.stored.single.entityId, 7);

    await tester.tap(find.text('Following'));
    await tester.pumpAndSettle();

    expect(find.text('Follow'), findsOneWidget);
    expect(repo.stored, isEmpty);
  });

  testWidgets('signed-out tap prompts sign-in and writes nothing',
      (tester) async {
    final repo = FakePersonalizationRepository();
    await tester.pumpWidget(_wrap(repo, signedIn: false));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Follow'));
    await tester.pumpAndSettle();

    expect(find.textContaining('Sign in from Profile'), findsOneWidget);
    expect(repo.followCalls, 0);
    expect(find.text('Follow'), findsOneWidget, reason: 'state must not flip');
  });

  testWidgets('a failed follow rolls the optimistic update back',
      (tester) async {
    // Load succeeds, write fails — the case that needs a rollback.
    final repo = FakePersonalizationRepository(
        failWritesWith: const ApiException('offline'));
    await tester.pumpWidget(_wrap(repo));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Follow'));
    await tester.pumpAndSettle();

    // The optimistic flip must not survive a failed write.
    expect(find.text('Follow'), findsOneWidget);
    expect(find.text('Following'), findsNothing);
    expect(find.text('offline'), findsOneWidget);
  });

  test('FollowKind ignores unknown wire values instead of crashing', () {
    expect(FollowKind.fromWire('athlete'), FollowKind.athlete);
    expect(FollowKind.fromWire('stadium'), isNull);
    expect(
      Follow.fromJson(const {'entity_type': 'stadium', 'entity_id': 3}),
      isNull,
    );
  });

  testWidgets('a failed load does not fabricate an empty follow list',
      (tester) async {
    // With no known-good list, toggling must refetch rather than invent state.
    final repo =
        FakePersonalizationRepository(failWith: const ApiException('down'));
    await tester.pumpWidget(_wrap(repo));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Follow'));
    await tester.pumpAndSettle();

    expect(find.text('Following'), findsNothing,
        reason: 'must not claim a follow that was never written');
  });

}
