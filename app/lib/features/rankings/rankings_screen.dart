import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/competitive_repository.dart';
import '../../domain/competitive_models.dart';

typedef LadderKey = ({String scope, String methodology, String? discipline});

final ladderProvider =
    FutureProvider.autoDispose.family<RankingLadder, LadderKey>(
  (ref, key) => ref.watch(competitiveRepositoryProvider).rankings(
        scope: key.scope,
        methodology: key.methodology,
        discipline: key.discipline,
      ),
  retry: (count, error) => null,
);

/// Ladders on offer. Methodology is explicit per ladder — the platform does
/// not assume every sport ranks the same way.
const _ladders = <(String, LadderKey)>[
  ('Sprints', (scope: 'athlete', methodology: 'world_ranking', discipline: 'track-field')),
  ('Marathon', (scope: 'athlete', methodology: 'world_ranking', discipline: 'marathon')),
  ('Swimming', (scope: 'athlete', methodology: 'world_ranking', discipline: 'swimming')),
  ('Nations', (scope: 'country', methodology: 'medal_count', discipline: null)),
];

class RankingsScreen extends ConsumerStatefulWidget {
  const RankingsScreen({super.key});

  @override
  ConsumerState<RankingsScreen> createState() => _RankingsScreenState();
}

class _RankingsScreenState extends ConsumerState<RankingsScreen> {
  int _index = 0;

  @override
  Widget build(BuildContext context) {
    final (label, key) = _ladders[_index];
    final ladder = ref.watch(ladderProvider(key));
    return Scaffold(
      appBar: AppBar(title: const Text('Rankings')),
      body: Column(
        children: [
          SizedBox(
            height: 48,
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
              itemCount: _ladders.length,
              separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
              itemBuilder: (context, i) => FilterChip(
                label: Text(_ladders[i].$1),
                selected: _index == i,
                onSelected: (_) => setState(() => _index = i),
              ),
            ),
          ),
          Expanded(
            child: ladder.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(ladderProvider(key)),
              ),
              data: (data) => data.entries.isEmpty
                  ? EmptyState(
                      icon: Icons.leaderboard_outlined,
                      title: 'No $label ranking yet',
                      message: 'Ladders appear once ranking data is published.',
                    )
                  : ListView(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      children: [
                        _LadderHeader(ladder: data),
                        for (final entry in data.entries)
                          _RankRow(entry: entry, scope: data.scope),
                      ],
                    ),
            ),
          ),
        ],
      ),
    );
  }
}

class _LadderHeader extends StatelessWidget {
  const _LadderHeader({required this.ladder});

  final RankingLadder ladder;

  static const _methodologyLabels = {
    'world_ranking': 'World ranking points',
    'olympic_ranking': 'Olympic ranking',
    'season_points': 'Season points',
    'elo': 'Elo rating',
    'medal_count': 'Medal count',
  };

  @override
  Widget build(BuildContext context) {
    final asOf = ladder.asOf;
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Text(
        [
          _methodologyLabels[ladder.methodology] ?? ladder.methodology,
          if (asOf != null)
            'as of ${MaterialLocalizations.of(context).formatShortDate(asOf)}',
        ].join(' · '),
        style: Theme.of(context).textTheme.bodySmall,
      ),
    );
  }
}

class _RankRow extends StatelessWidget {
  const _RankRow({required this.entry, required this.scope});

  final RankingEntry entry;
  final String scope;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final slug = entry.entitySlug;
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: ListTile(
          leading: CircleAvatar(
            radius: 16,
            backgroundColor: entry.rank <= 3
                ? AppColors.medalGold.withValues(alpha: 0.85)
                : theme.colorScheme.onSurface.withValues(alpha: 0.08),
            child: Text('${entry.rank}',
                style: theme.textTheme.labelLarge?.copyWith(
                    color: entry.rank <= 3 ? Colors.black87 : null)),
          ),
          title: Text(entry.entityName ?? 'Unknown'),
          subtitle:
              entry.entitySubtitle == null ? null : Text(entry.entitySubtitle!),
          trailing: entry.points == null
              ? null
              : Text(
                  entry.points!.toStringAsFixed(0),
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w800,
                    fontFeatures: const [FontFeature.tabularFigures()],
                  ),
                ),
          onTap: slug == null
              ? null
              : () => context.push(
                    scope == 'country' ? '/countries/$slug' : '/athletes/$slug',
                  ),
        ),
      ),
    );
  }
}
