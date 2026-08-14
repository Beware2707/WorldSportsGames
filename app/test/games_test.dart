import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/data/games_repository.dart';
import 'package:world_sports_games/domain/game_models.dart';
import 'package:world_sports_games/domain/models.dart';
import 'package:world_sports_games/features/games/engines/game_engine.dart';
import 'package:world_sports_games/core/network/api_client.dart';
import 'package:world_sports_games/features/games/engines/sequence_engine.dart';
import 'package:world_sports_games/features/games/game_play_screen.dart';
import 'package:world_sports_games/features/games/games_screen.dart';
import 'package:world_sports_games/features/profile/profile_screen.dart';

const _user = UserAccount(id: 1, email: 'a@example.com', displayName: 'A');

class _SignedIn extends CurrentUserNotifier {
  @override
  UserAccount? build() => _user;
}

const _reactionGame = Game(
  id: 1,
  code: 'sprint-reaction',
  name: '100m Reaction',
  engine: GameEngine.reaction,
  config: {'rounds': 2},
  scoreDirection: 'lower_better',
  scoreUnit: 'ms',
  tagline: 'Beat the gun',
);

const _futureGame = Game(
  id: 2,
  code: 'hologram-hurdles',
  name: 'Hologram Hurdles',
  engine: GameEngine.unsupported,
  config: {},
  scoreDirection: 'higher_better',
);

class FakeGamesRepository implements GamesRepository {
  FakeGamesRepository({this.catalogue = const [_reactionGame], this.submitError});

  final List<Game> catalogue;
  final Exception? submitError;
  final submitted = <(String, double)>[];
  ScoreResult next = const ScoreResult(
    score: 250,
    xpAwarded: 25,
    isPersonalBest: true,
    totalXp: 25,
    level: 1,
    xpIntoLevel: 25,
    xpForNextLevel: 100,
    streakDays: 1,
    unlocked: [
      GameAchievement(
          code: 'first-play', name: 'First Whistle', description: 'Play once.'),
    ],
  );

  @override
  Future<List<Game>> games() async => catalogue;

  @override
  Future<Game> dailyChallenge() async => catalogue.first;

  @override
  Future<ScoreResult> submitScore(
      String code, double score, Map<String, dynamic> detail) async {
    submitted.add((code, score));
    final err = submitError;
    if (err != null) throw err;
    return next;
  }

  @override
  Future<Leaderboard> leaderboard(String code,
          {String scope = 'global', String? country}) async =>
      Leaderboard(
          gameCode: code,
          scope: scope,
          scoreDirection: 'lower_better',
          rows: const []);

  @override
  Future<GameProgress> progress() async => const GameProgress(
        totalXp: 25,
        level: 1,
        xpIntoLevel: 25,
        xpForNextLevel: 100,
        sessionsPlayed: 1,
        streakDays: 2,
        achievements: [],
        lockedAchievements: [],
      );
}

Widget _wrap(Widget child, FakeGamesRepository repo, {bool signedIn = true}) =>
    ProviderScope(
      overrides: [
        gamesRepositoryProvider.overrideWithValue(repo),
        if (signedIn) currentUserProvider.overrideWith(() => _SignedIn()),
      ],
      child: MaterialApp(home: child),
    );

void main() {
  group('models', () {
    test('unknown engine degrades instead of throwing', () {
      expect(GameEngine.fromWire('reaction'), GameEngine.reaction);
      expect(GameEngine.fromWire('teleport'), GameEngine.unsupported);
    });

    test('score formatting respects direction and unit', () {
      expect(_reactionGame.lowerIsBetter, isTrue);
      expect(_reactionGame.formatScore(249.6), '250 ms');

      const accuracy = Game(
        id: 3,
        code: 'free-throw',
        name: 'Free throw',
        engine: GameEngine.accuracy,
        config: {},
        scoreDirection: 'higher_better',
        scoreUnit: 'baskets',
      );
      expect(accuracy.lowerIsBetter, isFalse);
      expect(accuracy.formatScore(7), '7 baskets');
    });

    test('config falls back when the server omits a key', () {
      const bare = Game(
        id: 4,
        code: 'x',
        name: 'X',
        engine: GameEngine.reaction,
        config: {},
        scoreDirection: 'lower_better',
      );
      expect(bare.configInt('rounds', 5), 5);
      expect(_reactionGame.configInt('rounds', 5), 2);
    });

    test('level progress fraction is bounded', () {
      const progress = GameProgress(
        totalXp: 150,
        level: 2,
        xpIntoLevel: 50,
        xpForNextLevel: 200,
        sessionsPlayed: 3,
        streakDays: 1,
        achievements: [],
        lockedAchievements: [],
      );
      expect(progress.levelFraction, 0.25);
    });
  });

  group('engine registry', () {
    test('resolves known mechanics and refuses unknown ones', () {
      final registry = EngineRegistry({
        GameEngine.reaction: (game, onFinished) =>
            throw UnimplementedError('not built in this test'),
      });
      expect(registry.supports(GameEngine.reaction), isTrue);
      expect(registry.supports(GameEngine.unsupported), isFalse);
      expect(registry.build(_futureGame, (_, _) {}), isNull);
    });
  });

  group('games screen', () {
    testWidgets('lists games and shows the daily challenge', (tester) async {
      await tester.pumpWidget(
          _wrap(const GamesScreen(), FakeGamesRepository()));
      await tester.pumpAndSettle();

      expect(find.text('Daily challenge'), findsOneWidget);
      expect(find.text('100m Reaction'), findsWidgets);
      expect(find.text('All games'), findsOneWidget);
    });

    testWidgets('an unsupported game says so instead of failing silently',
        (tester) async {
      await tester.pumpWidget(_wrap(
        const GamesScreen(),
        FakeGamesRepository(catalogue: const [_futureGame]),
      ));
      await tester.pumpAndSettle();

      expect(find.text('Update the app to play this'), findsOneWidget);
    });

    testWidgets('signed-out players are told scores are not saved',
        (tester) async {
      await tester.pumpWidget(_wrap(
        const GamesScreen(),
        FakeGamesRepository(),
        signedIn: false,
      ));
      await tester.pumpAndSettle();

      expect(find.text('Sign in to save scores'), findsOneWidget);
    });
  });

  group('play screen', () {
    testWidgets('a full reaction run submits a score and shows the reward',
        (tester) async {
      final repo = FakeGamesRepository();
      await tester.pumpWidget(
          _wrap(const GamePlayScreen(code: 'sprint-reaction'), repo));
      await tester.pumpAndSettle();

      // Two rounds: tap to arm, wait past the random delay, tap on green.
      for (var round = 0; round < 2; round++) {
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
        await tester.pump(const Duration(seconds: 5)); // past max hold
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
      }
      await tester.pumpAndSettle();

      expect(repo.submitted.length, 1);
      expect(repo.submitted.single.$1, 'sprint-reaction');
      expect(find.text('Your score'), findsOneWidget);
      expect(find.text('+25 XP'), findsOneWidget);
      expect(find.text('Personal best'), findsOneWidget);
      expect(find.text('First Whistle'), findsOneWidget);
    });

    testWidgets('a false start does not submit a score', (tester) async {
      final repo = FakeGamesRepository();
      await tester.pumpWidget(
          _wrap(const GamePlayScreen(code: 'sprint-reaction'), repo));
      await tester.pumpAndSettle();

      await tester.tap(find.byType(GestureDetector).first); // arm
      await tester.pump();
      await tester.tap(find.byType(GestureDetector).first); // too early
      await tester.pump();

      expect(find.text('False start'), findsOneWidget);
      expect(repo.submitted, isEmpty);
    });

    testWidgets('signed-out play is allowed but not saved', (tester) async {
      final repo = FakeGamesRepository();
      await tester.pumpWidget(_wrap(
        const GamePlayScreen(code: 'sprint-reaction'),
        repo,
        signedIn: false,
      ));
      await tester.pumpAndSettle();

      for (var round = 0; round < 2; round++) {
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
        await tester.pump(const Duration(seconds: 5));
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
      }
      await tester.pumpAndSettle();

      expect(repo.submitted, isEmpty, reason: 'no account, no submission');
      expect(find.text('Score not saved'), findsOneWidget);
    });

    testWidgets('an unknown game code shows not-found', (tester) async {
      await tester.pumpWidget(
          _wrap(const GamePlayScreen(code: 'nope'), FakeGamesRepository()));
      await tester.pumpAndSettle();

      expect(find.text('Game not found'), findsOneWidget);
    });

    testWidgets('a failed submission does not tell a signed-in player to sign in',
        (tester) async {
      final repo = FakeGamesRepository(
          submitError: const ApiException('Could not record that play'));
      await tester.pumpWidget(
          _wrap(const GamePlayScreen(code: 'sprint-reaction'), repo));
      await tester.pumpAndSettle();

      for (var round = 0; round < 2; round++) {
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
        await tester.pump(const Duration(seconds: 5));
        await tester.tap(find.byType(GestureDetector).first);
        await tester.pump();
      }
      await tester.pumpAndSettle();

      expect(find.text('Score not saved'), findsOneWidget);
      // The real reason (shown in the panel and echoed in the snackbar),
      // not a misleading "sign in" prompt.
      expect(find.textContaining('Could not record that play'), findsWidgets);
      expect(find.textContaining('Play again to retry'), findsOneWidget);
      expect(find.textContaining('Sign in to earn XP'), findsNothing);
    });
  });

  group('sequence engine', () {
    testWidgets('extra taps after the run ends do not crash or resubmit',
        (tester) async {
      var finishCalls = 0;
      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: SequenceEngine(
            game: const Game(
              id: 9,
              code: 'ski-gates',
              name: 'Ski Racing',
              engine: GameEngine.sequence,
              config: {'gates': 2},
              scoreDirection: 'higher_better',
            ),
            onFinished: (score, detail) => finishCalls++,
          ),
        ),
      ));
      // Let the reveal animation play out.
      await tester.pump(const Duration(seconds: 3));

      // Tap every tile several times: at least one is wrong, ending the run,
      // and further taps must not index past the pattern or resubmit.
      for (var pass = 0; pass < 3; pass++) {
        for (var tile = 0; tile < 6; tile++) {
          final finder = find.text('${tile + 1}');
          if (finder.evaluate().isNotEmpty) {
            await tester.tap(finder.first, warnIfMissed: false);
            await tester.pump();
          }
        }
      }

      expect(tester.takeException(), isNull, reason: 'no RangeError after the run');
      expect(finishCalls, 1, reason: 'the run must finish exactly once');
    });
  });
}