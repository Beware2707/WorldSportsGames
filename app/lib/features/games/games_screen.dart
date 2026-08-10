import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../domain/game_models.dart';
import 'games_providers.dart';

class GamesScreen extends ConsumerWidget {
  const GamesScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final games = ref.watch(gamesProvider);
    final daily = ref.watch(dailyChallengeProvider);
    final progress = ref.watch(gameProgressProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Games')),
      body: RefreshIndicator(
        onRefresh: () async {
          ref.invalidate(gamesProvider);
          ref.invalidate(dailyChallengeProvider);
          ref.invalidate(gameProgressProvider);
        },
        child: games.when(
          loading: () => const Center(child: CircularProgressIndicator()),
          error: (e, _) => ErrorState(
            message: e.toString(),
            onRetry: () => ref.invalidate(gamesProvider),
          ),
          data: (items) => ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.only(bottom: AppSpacing.xl),
            children: [
              progress.maybeWhen(
                data: (p) => p == null
                    ? const _SignInPrompt()
                    : _ProgressCard(progress: p),
                orElse: () => const SizedBox.shrink(),
              ),
              daily.maybeWhen(
                data: (game) => _DailyCard(game: game),
                orElse: () => const SizedBox.shrink(),
              ),
              const SectionHeader('All games'),
              for (final game in items)
                Padding(
                  padding: const EdgeInsets.fromLTRB(
                      AppSpacing.md, 0, AppSpacing.md, AppSpacing.sm),
                  child: _GameCard(game: game),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

class _SignInPrompt extends StatelessWidget {
  const _SignInPrompt();

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(AppSpacing.md),
      child: Card(
        child: ListTile(
          leading: const Icon(Icons.videogame_asset_outlined),
          title: const Text('Sign in to save scores'),
          subtitle: const Text('You can play now — XP, streaks and '
              'leaderboards need an account.'),
          trailing: const Icon(Icons.chevron_right_rounded),
          onTap: () => GoRouter.of(context).go('/profile'),
        ),
      ),
    );
  }
}

class _ProgressCard extends StatelessWidget {
  const _ProgressCard({required this.progress});

  final GameProgress progress;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.all(AppSpacing.md),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.lg),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  CircleAvatar(
                    radius: 22,
                    backgroundColor:
                        theme.colorScheme.primary.withValues(alpha: 0.15),
                    child: Text('${progress.level}',
                        style: theme.textTheme.titleMedium
                            ?.copyWith(color: theme.colorScheme.primary)),
                  ),
                  const SizedBox(width: AppSpacing.md),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text('Level ${progress.level}',
                            style: theme.textTheme.titleMedium),
                        Text('${progress.totalXp} XP · '
                            '${progress.sessionsPlayed} games'),
                      ],
                    ),
                  ),
                  if (progress.streakDays > 0)
                    Chip(
                      avatar: const Icon(Icons.local_fire_department_rounded,
                          size: 18),
                      label: Text('${progress.streakDays}d'),
                      visualDensity: VisualDensity.compact,
                    ),
                ],
              ),
              const SizedBox(height: AppSpacing.md),
              ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: LinearProgressIndicator(
                  value: progress.levelFraction,
                  minHeight: 8,
                ),
              ),
              const SizedBox(height: AppSpacing.xs),
              Text(
                '${progress.xpIntoLevel} / ${progress.xpForNextLevel} XP to '
                'level ${progress.level + 1}',
                style: theme.textTheme.bodySmall,
              ),
              if (progress.achievements.isNotEmpty) ...[
                const SizedBox(height: AppSpacing.md),
                Wrap(
                  spacing: AppSpacing.sm,
                  runSpacing: AppSpacing.xs,
                  children: [
                    for (final achievement in progress.achievements)
                      Tooltip(
                        message: achievement.description,
                        child: Chip(
                          avatar: const Icon(Icons.emoji_events_rounded,
                              size: 18, color: AppColors.medalGold),
                          label: Text(achievement.name),
                          visualDensity: VisualDensity.compact,
                        ),
                      ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class _DailyCard extends StatelessWidget {
  const _DailyCard({required this.game});

  final Game game;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(
          AppSpacing.md, 0, AppSpacing.md, AppSpacing.sm),
      child: Card(
        clipBehavior: Clip.antiAlias,
        color: theme.colorScheme.primary.withValues(alpha: 0.10),
        child: InkWell(
          onTap: () => GoRouter.of(context).push('/games/${game.code}'),
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.lg),
            child: Row(
              children: [
                Icon(Icons.today_rounded, color: theme.colorScheme.primary),
                const SizedBox(width: AppSpacing.md),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('Daily challenge',
                          style: theme.textTheme.labelLarge
                              ?.copyWith(color: theme.colorScheme.primary)),
                      const SizedBox(height: AppSpacing.xs),
                      Text(game.name, style: theme.textTheme.titleMedium),
                    ],
                  ),
                ),
                const Icon(Icons.chevron_right_rounded),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _GameCard extends ConsumerWidget {
  const _GameCard({required this.game});

  final Game game;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final supported = ref.watch(engineRegistryProvider).supports(game.engine);
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: supported
            ? () => GoRouter.of(context).push('/games/${game.code}')
            : null,
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Row(
            children: [
              Icon(sportIcon(game.sport?.icon),
                  color: theme.colorScheme.primary),
              const SizedBox(width: AppSpacing.md),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(game.name, style: theme.textTheme.titleSmall),
                    if (game.tagline != null)
                      Text(game.tagline!,
                          style: theme.textTheme.bodySmall,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis),
                    if (!supported)
                      Text('Update the app to play this',
                          style: theme.textTheme.bodySmall
                              ?.copyWith(color: AppColors.energy)),
                  ],
                ),
              ),
              if (game.personalBest != null)
                Column(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Text('Best', style: theme.textTheme.labelSmall),
                    Text(game.formatScore(game.personalBest!),
                        style: theme.textTheme.titleSmall
                            ?.copyWith(fontWeight: FontWeight.w800)),
                  ],
                )
              else
                const Icon(Icons.chevron_right_rounded),
            ],
          ),
        ),
      ),
    );
  }
}
