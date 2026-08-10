import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/games_repository.dart';
import '../../domain/game_models.dart';
import '../profile/profile_screen.dart';
import 'games_providers.dart';

class GamePlayScreen extends ConsumerStatefulWidget {
  const GamePlayScreen({super.key, required this.code});

  final String code;

  @override
  ConsumerState<GamePlayScreen> createState() => _GamePlayScreenState();
}

class _GamePlayScreenState extends ConsumerState<GamePlayScreen> {
  ScoreResult? _result;
  double? _localScore;
  bool _submitting = false;
  int _attemptKey = 0;

  Future<void> _onFinished(
      Game game, double score, Map<String, dynamic> detail) async {
    setState(() {
      _localScore = score;
      _submitting = true;
    });

    // Signed-out players still get to play; the score simply isn't saved,
    // and the UI says so rather than silently discarding it.
    if (ref.read(currentUserProvider) == null) {
      setState(() => _submitting = false);
      return;
    }

    final messenger = ScaffoldMessenger.of(context);
    try {
      final result = await ref
          .read(gamesRepositoryProvider)
          .submitScore(game.code, score, detail);
      if (!mounted) return;
      setState(() => _result = result);
      ref.invalidate(gamesProvider);
      ref.invalidate(gameProgressProvider);
      ref.invalidate(leaderboardProvider);
    } on ApiException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    } finally {
      if (mounted) setState(() => _submitting = false);
    }
  }

  void _playAgain() => setState(() {
        _result = null;
        _localScore = null;
        _attemptKey++;
      });

  @override
  Widget build(BuildContext context) {
    final games = ref.watch(gamesProvider);
    return games.when(
      loading: () => const Scaffold(
          body: Center(child: CircularProgressIndicator())),
      error: (e, _) => Scaffold(
        appBar: AppBar(),
        body: ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(gamesProvider),
        ),
      ),
      data: (items) {
        final game =
            items.where((g) => g.code == widget.code).firstOrNull;
        if (game == null) {
          return Scaffold(
            appBar: AppBar(),
            body: const EmptyState(
              icon: Icons.videogame_asset_off_rounded,
              title: 'Game not found',
            ),
          );
        }

        final engine = ref
            .read(engineRegistryProvider)
            .build(game, (score, detail) => _onFinished(game, score, detail));

        return Scaffold(
          appBar: AppBar(
            title: Text(game.name),
            actions: [
              IconButton(
                icon: const Icon(Icons.leaderboard_rounded),
                tooltip: 'Leaderboard',
                onPressed: () =>
                    context.push('/games/${game.code}/leaderboard'),
              ),
            ],
          ),
          body: _localScore != null
              ? _ResultPanel(
                  game: game,
                  score: _localScore!,
                  result: _result,
                  submitting: _submitting,
                  onPlayAgain: _playAgain,
                )
              : engine ??
                  const EmptyState(
                    icon: Icons.system_update_rounded,
                    title: 'This game needs a newer app version',
                    message:
                        'Your app does not know how to play this mechanic yet.',
                  ),
          // Rebuild the engine cleanly on replay.
          key: ValueKey('${game.code}-$_attemptKey'),
        );
      },
    );
  }
}

class _ResultPanel extends StatelessWidget {
  const _ResultPanel({
    required this.game,
    required this.score,
    required this.result,
    required this.submitting,
    required this.onPlayAgain,
  });

  final Game game;
  final double score;
  final ScoreResult? result;
  final bool submitting;
  final VoidCallback onPlayAgain;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ListView(
      padding: const EdgeInsets.all(AppSpacing.md),
      children: [
        Card(
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.xl),
            child: Column(
              children: [
                Text('Your score', style: theme.textTheme.labelLarge),
                const SizedBox(height: AppSpacing.sm),
                Text(
                  game.formatScore(score),
                  style: theme.textTheme.headlineMedium
                      ?.copyWith(fontWeight: FontWeight.w800),
                ),
                if (result?.isPersonalBest ?? false) ...[
                  const SizedBox(height: AppSpacing.sm),
                  Chip(
                    avatar: const Icon(Icons.star_rounded,
                        size: 18, color: AppColors.medalGold),
                    label: const Text('Personal best'),
                  ),
                ],
              ],
            ),
          ),
        ),
        const SizedBox(height: AppSpacing.md),
        if (submitting)
          const Center(child: CircularProgressIndicator())
        else if (result != null) ...[
          Card(
            child: Column(
              children: [
                ListTile(
                  leading: const Icon(Icons.bolt_rounded),
                  title: Text('+${result!.xpAwarded} XP'),
                  subtitle: Text('Level ${result!.level} · '
                      '${result!.totalXp} XP total'),
                ),
                if (result!.streakDays > 1)
                  ListTile(
                    leading: const Icon(Icons.local_fire_department_rounded),
                    title: Text('${result!.streakDays}-day streak'),
                  ),
              ],
            ),
          ),
          if (result!.unlocked.isNotEmpty) ...[
            const SectionHeader('Unlocked'),
            for (final achievement in result!.unlocked)
              Card(
                child: ListTile(
                  leading: const Icon(Icons.emoji_events_rounded,
                      color: AppColors.medalGold),
                  title: Text(achievement.name),
                  subtitle: Text(achievement.description),
                ),
              ),
          ],
        ] else
          Card(
            child: ListTile(
              leading: const Icon(Icons.info_outline_rounded),
              title: const Text('Score not saved'),
              subtitle: const Text('Sign in to earn XP and appear on '
                  'leaderboards.'),
              trailing: const Icon(Icons.chevron_right_rounded),
              onTap: () => GoRouter.of(context).go('/profile'),
            ),
          ),
        const SizedBox(height: AppSpacing.md),
        FilledButton.icon(
          onPressed: onPlayAgain,
          icon: const Icon(Icons.refresh_rounded),
          label: const Text('Play again'),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(52)),
        ),
      ],
    );
  }
}
