import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../data/personalization_repository.dart';
import '../../domain/personalization_models.dart';
import '../profile/profile_screen.dart';

/// The signed-in user's follows.
///
/// Holds an empty list when signed out rather than erroring, so follow
/// affordances can render everywhere and only *acting* on them requires auth.
/// Rebuilds when the user changes, so one account never sees another's follows.
class FollowsController extends AsyncNotifier<List<Follow>> {
  @override
  Future<List<Follow>> build() async {
    final user = ref.watch(currentUserProvider);
    if (user == null) return const [];
    return ref.watch(personalizationRepositoryProvider).follows();
  }

  bool isFollowing(FollowKind kind, int entityId) =>
      (state.value ?? const []).any(
        (f) => f.kind == kind && f.entityId == entityId,
      );

  /// Toggle a follow. Optimistic: the UI flips immediately and rolls back if
  /// the request fails, so a tap never appears to do nothing.
  Future<void> toggle(FollowKind kind, int entityId, {String? name}) async {
    final current = state.value ?? const <Follow>[];
    final following = isFollowing(kind, entityId);
    final repo = ref.read(personalizationRepositoryProvider);

    state = AsyncData(
      following
          ? [
              for (final f in current)
                if (!(f.kind == kind && f.entityId == entityId)) f,
            ]
          : [...current, Follow(kind: kind, entityId: entityId, name: name)],
    );

    try {
      List<Follow> updated;
      if (following) {
        await repo.unfollow(kind, entityId);
        updated = await repo.follows();
      } else {
        updated = await repo.follow(kind, entityId);
      }
      if (!ref.mounted) return;
      state = AsyncData(updated);
    } catch (error, stack) {
      if (!ref.mounted) return;
      state = AsyncData(current); // roll back to the truth
      Error.throwWithStackTrace(error, stack);
    }
  }
}

final followsProvider =
    AsyncNotifierProvider<FollowsController, List<Follow>>(
        FollowsController.new, retry: (count, error) => null);

/// Whether a specific entity is followed — cheap to watch per widget.
final isFollowingProvider =
    Provider.family<bool, (FollowKind, int)>((ref, key) {
  final follows = ref.watch(followsProvider).value ?? const [];
  return follows.any((f) => f.kind == key.$1 && f.entityId == key.$2);
});
