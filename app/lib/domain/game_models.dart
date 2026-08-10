/// Games, progression and leaderboards.
library;

import 'models.dart';

/// A client-side mechanic. Adding a *game* needs no new value here; adding a
/// new kind of interaction does.
enum GameEngine {
  reaction,
  accuracy,
  timing,
  sequence,
  unsupported;

  static GameEngine fromWire(String value) => switch (value) {
        'reaction' => GameEngine.reaction,
        'accuracy' => GameEngine.accuracy,
        'timing' => GameEngine.timing,
        'sequence' => GameEngine.sequence,
        // A newer server may ship a mechanic this build cannot render. Show
        // it as unavailable rather than crashing the catalogue.
        _ => GameEngine.unsupported,
      };
}

class Game {
  const Game({
    required this.id,
    required this.code,
    required this.name,
    required this.engine,
    required this.config,
    required this.scoreDirection,
    this.tagline,
    this.scoreUnit,
    this.sport,
    this.personalBest,
  });

  final int id;
  final String code;
  final String name;
  final GameEngine engine;
  final Map<String, dynamic> config;
  final String scoreDirection;
  final String? tagline;
  final String? scoreUnit;
  final Sport? sport;
  final double? personalBest;

  bool get lowerIsBetter => scoreDirection == 'lower_better';

  int configInt(String key, int fallback) {
    final value = config[key];
    return value is int ? value : fallback;
  }

  String formatScore(double score) {
    final rounded = lowerIsBetter ? score.round().toString() : _trim(score);
    return scoreUnit == null ? rounded : '$rounded ${scoreUnit!}';
  }

  static String _trim(double value) =>
      value == value.roundToDouble() ? value.round().toString() : value.toStringAsFixed(1);

  factory Game.fromJson(Map<String, dynamic> json) => Game(
        id: json['id'] as int,
        code: json['code'] as String,
        name: json['name'] as String,
        tagline: json['tagline'] as String?,
        engine: GameEngine.fromWire(json['engine'] as String),
        config: (json['config'] as Map?)?.cast<String, dynamic>() ?? const {},
        scoreDirection: json['score_direction'] as String,
        scoreUnit: json['score_unit'] as String?,
        sport: json['sport'] == null
            ? null
            : Sport.fromJson(json['sport'] as Map<String, dynamic>),
        personalBest: (json['personal_best'] as num?)?.toDouble(),
      );
}

class GameAchievement {
  const GameAchievement({
    required this.code,
    required this.name,
    required this.description,
  });

  final String code;
  final String name;
  final String description;

  factory GameAchievement.fromJson(Map<String, dynamic> json) => GameAchievement(
        code: json['code'] as String,
        name: json['name'] as String,
        description: json['description'] as String,
      );
}

class ScoreResult {
  const ScoreResult({
    required this.score,
    required this.xpAwarded,
    required this.isPersonalBest,
    required this.totalXp,
    required this.level,
    required this.xpIntoLevel,
    required this.xpForNextLevel,
    required this.streakDays,
    required this.unlocked,
  });

  final double score;
  final int xpAwarded;
  final bool isPersonalBest;
  final int totalXp;
  final int level;
  final int xpIntoLevel;
  final int xpForNextLevel;
  final int streakDays;
  final List<GameAchievement> unlocked;

  factory ScoreResult.fromJson(Map<String, dynamic> json) => ScoreResult(
        score: (json['score'] as num).toDouble(),
        xpAwarded: json['xp_awarded'] as int,
        isPersonalBest: json['is_personal_best'] as bool,
        totalXp: json['total_xp'] as int,
        level: json['level'] as int,
        xpIntoLevel: json['xp_into_level'] as int,
        xpForNextLevel: json['xp_for_next_level'] as int,
        streakDays: json['streak_days'] as int,
        unlocked: (json['unlocked'] as List)
            .map((e) => GameAchievement.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class LeaderboardRow {
  const LeaderboardRow({
    required this.rank,
    required this.userId,
    required this.displayName,
    required this.score,
  });

  final int rank;
  final int userId;
  final String displayName;
  final double score;

  factory LeaderboardRow.fromJson(Map<String, dynamic> json) => LeaderboardRow(
        rank: json['rank'] as int,
        userId: json['user_id'] as int,
        displayName: json['display_name'] as String,
        score: (json['score'] as num).toDouble(),
      );
}

class Leaderboard {
  const Leaderboard({
    required this.gameCode,
    required this.scope,
    required this.scoreDirection,
    required this.rows,
    this.scopeLabel,
    this.scoreUnit,
  });

  final String gameCode;
  final String scope;
  final String scoreDirection;
  final List<LeaderboardRow> rows;
  final String? scopeLabel;
  final String? scoreUnit;

  factory Leaderboard.fromJson(Map<String, dynamic> json) => Leaderboard(
        gameCode: json['game_code'] as String,
        scope: json['scope'] as String,
        scopeLabel: json['scope_label'] as String?,
        scoreDirection: json['score_direction'] as String,
        scoreUnit: json['score_unit'] as String?,
        rows: (json['rows'] as List)
            .map((e) => LeaderboardRow.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class GameProgress {
  const GameProgress({
    required this.totalXp,
    required this.level,
    required this.xpIntoLevel,
    required this.xpForNextLevel,
    required this.sessionsPlayed,
    required this.streakDays,
    required this.achievements,
    required this.lockedAchievements,
  });

  final int totalXp;
  final int level;
  final int xpIntoLevel;
  final int xpForNextLevel;
  final int sessionsPlayed;
  final int streakDays;
  final List<GameAchievement> achievements;
  final List<GameAchievement> lockedAchievements;

  double get levelFraction =>
      xpForNextLevel == 0 ? 0 : xpIntoLevel / xpForNextLevel;

  factory GameProgress.fromJson(Map<String, dynamic> json) => GameProgress(
        totalXp: json['total_xp'] as int,
        level: json['level'] as int,
        xpIntoLevel: json['xp_into_level'] as int,
        xpForNextLevel: json['xp_for_next_level'] as int,
        sessionsPlayed: json['sessions_played'] as int,
        streakDays: json['streak_days'] as int,
        achievements: (json['achievements'] as List)
            .map((e) => GameAchievement.fromJson(e as Map<String, dynamic>))
            .toList(),
        lockedAchievements: (json['locked_achievements'] as List)
            .map((e) => GameAchievement.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}
