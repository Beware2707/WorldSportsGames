import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follow_button.dart';

final competitionDetailProvider =
    FutureProvider.autoDispose.family<Competition, String>(
  (ref, slug) => ref.watch(catalogRepositoryProvider).getCompetition(slug),
  retry: (count, error) => null,
);

// Note: no entry maps to AppColors.live. An edition being in progress is not
// live coverage and must never borrow the LIVE treatment.
const _statusColors = {
  'in_progress': AppColors.energy,
  'upcoming': AppColors.primary,
  'completed': AppColors.secondary,
};

const _statusLabels = {
  'in_progress': 'IN PROGRESS',
  'upcoming': 'UPCOMING',
  'completed': 'COMPLETED',
};

String _levelLabel(String level) => switch (level) {
      'olympic' => 'Olympic',
      'world' => 'World',
      'continental' => 'Continental',
      'league' => 'League',
      'national' => 'National',
      _ => 'Other',
    };

class CompetitionDetailScreen extends ConsumerWidget {
  const CompetitionDetailScreen({super.key, required this.slug});

  final String slug;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final competition = ref.watch(competitionDetailProvider(slug));
    return Scaffold(
      appBar: AppBar(title: Text(competition.value?.name ?? '')),
      body: competition.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(competitionDetailProvider(slug)),
        ),
        data: (c) => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(AppSpacing.lg),
                child: Row(
                  children: [
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(c.name,
                              style: Theme.of(context).textTheme.titleLarge),
                          const SizedBox(height: AppSpacing.xs),
                          Text(
                            [
                              _levelLabel(c.level),
                              if (c.sport != null) c.sport!.name,
                            ].join(' · '),
                            style: Theme.of(context).textTheme.bodySmall,
                          ),
                        ],
                      ),
                    ),
                    FollowButton(
                      kind: FollowKind.competition,
                      entityId: c.id,
                      name: c.name,
                    ),
                  ],
                ),
              ),
            ),
            const SectionHeader('Editions'),
            if (c.editions.isEmpty)
              const EmptyState(
                  icon: Icons.calendar_month_rounded,
                  title: 'No editions announced yet'),
            for (final edition in c.editions)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                child: Card(
                  clipBehavior: Clip.antiAlias,
                  child: ListTile(
                    title: Text(edition.label),
                    subtitle: Text([
                      if (edition.hostCity != null) edition.hostCity!,
                      if (edition.hostCountry?.flagEmoji != null)
                        edition.hostCountry!.flagEmoji!,
                      '${edition.year}',
                    ].join(' · ')),
                    trailing: Chip(
                      label: Text(_statusLabels[edition.status] ??
                          edition.status.toUpperCase()),
                      labelStyle:
                          Theme.of(context).textTheme.labelSmall?.copyWith(
                                color: _statusColors[edition.status],
                                fontWeight: FontWeight.w800,
                              ),
                      backgroundColor: _statusColors[edition.status]
                              ?.withValues(alpha: 0.12) ??
                          Colors.transparent,
                      visualDensity: VisualDensity.compact,
                    ),
                    onTap: () => context
                        .push('/competitions/${c.slug}/editions/${edition.id}'),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
