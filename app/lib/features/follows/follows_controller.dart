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
  /// Bumped on every build. Riverpod reuses the Notifier instance across
  /// rebuilds, so `ref.mounted` stays true when a watched dependency changes —
  /// an in-flight toggle from the previous user would otherwise write its
  /// result over the new user's (empty) list.
  int _generation = 0;

  @override
  Future<List<Follow>> build() async {
    _generation++;
    final user = ref.watch(currentUserProvider);
    if (user == null) return const [];
    return ref.watch(personalizationRepositoryProvider).follows();
  }

  bool isFollowing(FollowKind kind, int entityId) =>
      (state.value ?? const []).any(
        (f) => f.kind == kind && f.entityId == entityId,
      );

  /// Toggle a follow.
  ///
  /// Optimistic: the UI flips immediately so a tap never appears to do
  /// nothing. Rollback is per-entry rather than a whole-list snapshot, so two
  /// overlapping toggles cannot clobber each other's result.
  Future<void> toggle(FollowKind kind, int entityId, {String? name}) async {
    final generation = _generation;

    // With no known-good list (still loading, or the fetch failed) an
    // optimistic edit would fabricate state. Refetch instead.
    // invalidateSelf, not invalidate(followsProvider): a notifier cannot
    // depend on its own provider.
    if (state.value == null) {
      ref.invalidateSelf();
      return;
    }

    final following = isFollowing(kind, entityId);
    final repo = ref.read(personalizationRepositoryProvider);

    _apply(kind, entityId, add: !following, name: name);

    try {
      List<Follow> updated;
      if (following) {
        await repo.unfollow(kind, entityId);
        updated = await repo.follows();
      } else {
        updated = await repo.follow(kind, entityId);
      }
      if (!ref.mounted || generation != _generation) return;
      state = AsyncData(updated);
    } catch (error, stack) {
      if (ref.mounted && generation == _generation) {
        _apply(kind, entityId, add: following, name: name); // undo just this one
      }
      Error.throwWithStackTrace(error, stack);
    }
  }

  void _apply(FollowKind kind, int entityId, {required bool add, String? name}) {
    final current = state.value ?? const <Follow>[];
    state = AsyncData(
      add
          ? [
              ...current.where((f) => !(f.kind == kind && f.entityId == entityId)),
              Follow(kind: kind, entityId: entityId, name: name),
            ]
          : [
              for (final f in current)
                if (!(f.kind == kind && f.entityId == entityId)) f,
            ],
    );
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

/// Canonical identity of the follow set.
///
/// The home feed depends on *which* entities are followed, not on the
/// AsyncValue instance. Watching this collapses the optimistic write and the
/// server's authoritative response — which carry the same set — into a single
/// change, so one tap triggers one refetch rather than two or three.
final followKeyProvider = Provider<String>((ref) {
  final follows = ref.watch(followsProvider).value ?? const [];
  final keys = [for (final f in follows) '${f.kind.name}:${f.entityId}']..sort();
  return keys.join(',');
});
