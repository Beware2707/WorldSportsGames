import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../domain/game_models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follows_controller.dart';
import 'games_providers.dart';

class LeaderboardScreen extends ConsumerStatefulWidget {
  const LeaderboardScreen({super.key, required this.code});

  final String code;

  @override
  ConsumerState<LeaderboardScreen> createState() => _LeaderboardScreenState();
}

class _LeaderboardScreenState extends ConsumerState<LeaderboardScreen> {
  String _scope = 'global';
  String? _country;

  @override
  Widget build(BuildContext context) {
    // Country scope needs a country: use the first one the user follows.
    final followedCountry = ref
        .watch(followsProvider)
        .value
        ?.where((f) => f.kind == FollowKind.country)
        .firstOrNull;
    _country ??= followedCountry?.slug;

    final key = (code: widget.code, scope: _scope, country: _country);
    final board = ref.watch(leaderboardProvider(key));

    return Scaffold(
      appBar: AppBar(title: const Text('Leaderboard')),
      body: Column(
        children: [
          SizedBox(
            height: 48,
            child: ListView(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
              children: [
                for (final scope in ['global', 'country', 'friends'])
                  Padding(
                    padding: const EdgeInsets.only(right: AppSpacing.sm),
                    child: FilterChip(
                      label: Text(switch (scope) {
                        'global' => 'Global',
                        'country' => followedCountry?.name ?? 'Country',
                        _ => 'Friends',
                      }),
                      selected: _scope == scope,
                      // Country scope is unavailable until the user follows one.
                      onSelected: scope == 'country' && _country == null
                          ? null
                          : (_) => setState(() => _scope = scope),
                    ),
                  ),
              ],
            ),
          ),
          Expanded(
            child: board.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(leaderboardProvider(key)),
              ),
              data: (data) => data.rows.isEmpty
                  ? EmptyState(
                      icon: Icons.leaderboard_outlined,
                      title: _emptyTitle(data),
                      message: _emptyMessage(data),
                    )
                  : ListView.builder(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      itemCount: data.rows.length,
                      itemBuilder: (context, i) =>
                          _Row(row: data.rows[i], board: data),
                    ),
            ),
          ),
        ],
      ),
    );
  }

  String _emptyTitle(Leaderboard board) => switch (board.scope) {
        'friends' => 'No friends leaderboard yet',
        'country' => 'No scores from ${board.scopeLabel ?? "this country"}',
        _ => 'No scores yet',
      };

  /// Honest about the friends board: there is no social graph to draw on, so
  /// say that rather than implying the user simply has no friends playing.
  String _emptyMessage(Leaderboard board) => switch (board.scope) {
        'friends' =>
          'Adding friends is not built yet — this board stays empty until it is.',
        'country' => 'Be the first to post a score.',
        _ => 'Play a round to put yourself on the board.',
      };
}

class _Row extends StatelessWidget {
  const _Row({required this.row, required this.board});

  final LeaderboardRow row;
  final Leaderboard board;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final value = board.scoreUnit == null
        ? row.score.toStringAsFixed(row.score == row.score.roundToDouble() ? 0 : 1)
        : '${row.score.toStringAsFixed(row.score == row.score.roundToDouble() ? 0 : 1)} ${board.scoreUnit}';

    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        child: ListTile(
          leading: CircleAvatar(
            radius: 16,
            backgroundColor: row.rank <= 3
                ? AppColors.medalGold.withValues(alpha: 0.85)
                : theme.colorScheme.onSurface.withValues(alpha: 0.08),
            child: Text('${row.rank}',
                style: theme.textTheme.labelLarge
                    ?.copyWith(color: row.rank <= 3 ? Colors.black87 : null)),
          ),
          title: Text(row.displayName),
          trailing: Text(
            value,
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w800,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ),
      ),
    );
  }
}
