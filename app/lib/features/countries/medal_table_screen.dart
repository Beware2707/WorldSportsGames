import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/competitive_repository.dart';
import '../../domain/competitive_models.dart';
import '../../domain/models.dart';

final medalEditionsProvider = FutureProvider.autoDispose<List<Edition>>(
  (ref) => ref.watch(competitiveRepositoryProvider).medalEditions(),
  retry: (count, error) => null,
);

final medalTableProvider =
    FutureProvider.autoDispose.family<MedalTable, int?>(
  (ref, editionId) =>
      ref.watch(competitiveRepositoryProvider).medalTable(editionId: editionId),
  retry: (count, error) => null,
);

class MedalTableScreen extends ConsumerStatefulWidget {
  const MedalTableScreen({super.key});

  @override
  ConsumerState<MedalTableScreen> createState() => _MedalTableScreenState();
}

class _MedalTableScreenState extends ConsumerState<MedalTableScreen> {
  int? _editionId;

  @override
  Widget build(BuildContext context) {
    final editions = ref.watch(medalEditionsProvider);
    final table = ref.watch(medalTableProvider(_editionId));

    return Scaffold(
      appBar: AppBar(title: const Text('Medal table')),
      body: Column(
        children: [
          SizedBox(
            height: 48,
            child: editions.when(
              loading: () => const SizedBox.shrink(),
              error: (_, _) => const SizedBox.shrink(),
              data: (items) => ListView(
                scrollDirection: Axis.horizontal,
                padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
                children: [
                  FilterChip(
                    label: const Text('All time'),
                    selected: _editionId == null,
                    onSelected: (_) => setState(() => _editionId = null),
                  ),
                  // Only editions that actually have medals are offered, so a
                  // filter never lands the user on an empty table.
                  for (final edition in items) ...[
                    const SizedBox(width: AppSpacing.sm),
                    FilterChip(
                      label: Text(edition.label),
                      selected: _editionId == edition.id,
                      onSelected: (_) =>
                          setState(() => _editionId = edition.id),
                    ),
                  ],
                ],
              ),
            ),
          ),
          Expanded(
            child: table.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(medalTableProvider(_editionId)),
              ),
              data: (data) => data.rows.isEmpty
                  ? const EmptyState(
                      icon: Icons.workspace_premium_outlined,
                      title: 'No medals recorded yet',
                    )
                  : ListView(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      children: [
                        const _TableHeader(),
                        for (final row in data.rows) _MedalRow(tally: row),
                      ],
                    ),
            ),
          ),
        ],
      ),
    );
  }
}

class _TableHeader extends StatelessWidget {
  const _TableHeader();

  @override
  Widget build(BuildContext context) {
    final style = Theme.of(context).textTheme.labelSmall;
    return Padding(
      padding: const EdgeInsets.fromLTRB(
          AppSpacing.md, 0, AppSpacing.md, AppSpacing.sm),
      child: Row(
        children: [
          SizedBox(width: 28, child: Text('#', style: style)),
          Expanded(child: Text('Country', style: style)),
          for (final label in ['G', 'S', 'B', 'Tot'])
            SizedBox(
              width: 34,
              child: Text(label, style: style, textAlign: TextAlign.end),
            ),
        ],
      ),
    );
  }
}

class _MedalRow extends StatelessWidget {
  const _MedalRow({required this.tally});

  final MedalTally tally;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    Widget count(int value, Color color) => SizedBox(
          width: 34,
          child: Text(
            '$value',
            textAlign: TextAlign.end,
            style: theme.textTheme.titleSmall?.copyWith(
              color: value == 0
                  ? theme.colorScheme.onSurface.withValues(alpha: 0.35)
                  : color,
              fontWeight: FontWeight.w700,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        );

    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: () => context.push('/countries/${tally.country.iso3}'),
          child: Padding(
            padding: const EdgeInsets.symmetric(
                horizontal: AppSpacing.md, vertical: AppSpacing.md),
            child: Row(
              children: [
                SizedBox(
                  width: 28,
                  child: Text('${tally.rank}',
                      style: theme.textTheme.titleSmall
                          ?.copyWith(fontWeight: FontWeight.w800)),
                ),
                Expanded(
                  child: Text(
                    '${tally.country.flagEmoji ?? ''} ${tally.country.name}'
                        .trim(),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                count(tally.gold, AppColors.medalGold),
                count(tally.silver, AppColors.medalSilver),
                count(tally.bronze, AppColors.medalBronze),
                SizedBox(
                  width: 34,
                  child: Text(
                    '${tally.total}',
                    textAlign: TextAlign.end,
                    style: theme.textTheme.titleSmall?.copyWith(
                      fontWeight: FontWeight.w800,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
