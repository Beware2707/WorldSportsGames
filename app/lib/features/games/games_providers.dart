import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../data/games_repository.dart';
import '../../domain/game_models.dart';
import '../profile/profile_screen.dart';
import 'engines/accuracy_engine.dart';
import 'engines/game_engine.dart';
import 'engines/reaction_engine.dart';
import 'engines/sequence_engine.dart';
import 'engines/timing_engine.dart';

/// Mechanics this build can render. A game whose engine is absent shows an
/// honest "update the app" panel rather than a broken screen.
final engineRegistryProvider = Provider<EngineRegistry>(
  (ref) => EngineRegistry({
    GameEngine.reaction: (game, onFinished) =>
        ReactionEngine(game: game, onFinished: onFinished),
    GameEngine.accuracy: (game, onFinished) =>
        AccuracyEngine(game: game, onFinished: onFinished),
    GameEngine.timing: (game, onFinished) =>
        TimingEngine(game: game, onFinished: onFinished),
    GameEngine.sequence: (game, onFinished) =>
        SequenceEngine(game: game, onFinished: onFinished),
  }),
);

/// Catalogue. Watches the user so personal bests refresh on sign-in.
final gamesProvider = FutureProvider.autoDispose<List<Game>>(
  (ref) {
    ref.watch(currentUserProvider);
    return ref.watch(gamesRepositoryProvider).games();
  },
  retry: (count, error) => null,
);

final dailyChallengeProvider = FutureProvider.autoDispose<Game>(
  (ref) {
    ref.watch(currentUserProvider);
    return ref.watch(gamesRepositoryProvider).dailyChallenge();
  },
  retry: (count, error) => null,
);

final gameProgressProvider = FutureProvider.autoDispose<GameProgress?>(
  (ref) async {
    if (ref.watch(currentUserProvider) == null) return null;
    return ref.watch(gamesRepositoryProvider).progress();
  },
  retry: (count, error) => null,
);

typedef LeaderboardKey = ({String code, String scope, String? country});

final leaderboardProvider =
    FutureProvider.autoDispose.family<Leaderboard, LeaderboardKey>(
  (ref, key) => ref.watch(gamesRepositoryProvider).leaderboard(
        key.code,
        scope: key.scope,
        country: key.country,
      ),
  retry: (count, error) => null,
);
