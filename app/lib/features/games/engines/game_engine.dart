import 'package:flutter/material.dart';

import '../../../domain/game_models.dart';

/// Contract every playable mechanic implements.
///
/// A game is data (`Game` row + config); a *mechanic* is code. Adding a new
/// game to the platform means seeding a row — only a genuinely new kind of
/// interaction requires a new engine here.
abstract class GameEngineWidget extends StatefulWidget {
  const GameEngineWidget({
    super.key,
    required this.game,
    required this.onFinished,
  });

  final Game game;

  /// Called once with the final score and any per-round detail. The server
  /// computes XP — the client never reports its own reward.
  final void Function(double score, Map<String, dynamic> detail) onFinished;
}

/// Resolves a game to its mechanic. Unknown engines render an honest
/// "not supported in this version" panel instead of a blank screen.
typedef EngineBuilder = GameEngineWidget Function(
  Game game,
  void Function(double score, Map<String, dynamic> detail) onFinished,
);

class EngineRegistry {
  EngineRegistry(this._builders);

  final Map<GameEngine, EngineBuilder> _builders;

  bool supports(GameEngine engine) => _builders.containsKey(engine);

  GameEngineWidget? build(
    Game game,
    void Function(double score, Map<String, dynamic> detail) onFinished,
  ) {
    final builder = _builders[game.engine];
    return builder?.call(game, onFinished);
  }
}
