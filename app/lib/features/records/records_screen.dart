import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/competitive_repository.dart';
import '../../domain/competitive_models.dart';

final recordsProvider =
    FutureProvider.autoDispose.family<List<SportRecord>, String?>(
  (ref, kind) => ref.watch(competitiveRepositoryProvider).records(kind: kind),
  retry: (count, error) => null,
);

const _kinds = <(String?, String)>[
  (null, 'All'),
  ('WR', 'World'),
  ('OR', 'Olympic'),
  ('NR', 'National'),
  ('PB', 'Personal'),
];

class RecordsScreen extends ConsumerStatefulWidget {
  const RecordsScreen({super.key});

  @override
  ConsumerState<RecordsScreen> createState() => _RecordsScreenState();
}

class _RecordsScreenState extends ConsumerState<RecordsScreen> {
  String? _kind = 'WR';

  @override
  Widget build(BuildContext context) {
    final records = ref.watch(recordsProvider(_kind));
    return Scaffold(
      appBar: AppBar(title: const Text('Records')),
      body: Column(
        children: [
          SizedBox(
            height: 48,
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
              itemCount: _kinds.length,
              separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
              itemBuilder: (context, i) {
                final (value, label) = _kinds[i];
                return FilterChip(
                  label: Text(label),
                  selected: _kind == value,
                  onSelected: (_) => setState(() => _kind = value),
                );
              },
            ),
          ),
          Expanded(
            child: records.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(recordsProvider(_kind)),
              ),
              data: (items) => items.isEmpty
                  ? const EmptyState(
                      icon: Icons.military_tech_outlined,
                      title: 'No records of this type yet',
                    )
                  : ListView.builder(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      itemCount: items.length,
                      itemBuilder: (context, i) =>
                          _RecordCard(record: items[i]),
                    ),
            ),
          ),
        ],
      ),
    );
  }
}

class _RecordCard extends StatelessWidget {
  const _RecordCard({required this.record});

  final SportRecord record;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final slug = record.holderSlug;
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: slug == null ? null : () => context.push('/athletes/$slug'),
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.md),
            child: Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Chip(
                            label: Text(record.kind),
                            labelStyle: theme.textTheme.labelSmall
                                ?.copyWith(fontWeight: FontWeight.w800),
                            visualDensity: VisualDensity.compact,
                            padding: EdgeInsets.zero,
                          ),
                          const SizedBox(width: AppSpacing.sm),
                          Flexible(
                            child: Text(
                              '${record.eventName} · ${_genderLabel(record.gender)}',
                              style: theme.textTheme.titleSmall,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: AppSpacing.xs),
                      Text(
                        [
                          record.holderName ?? 'Unknown holder',
                          if (record.country?.flagEmoji != null)
                            record.country!.flagEmoji!,
                          if (record.location != null) record.location!,
                        ].join(' · '),
                        style: theme.textTheme.bodySmall
                            ?.copyWith(color: theme.secondaryText),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: AppSpacing.sm),
                Column(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Text(
                      record.valueText,
                      style: theme.textTheme.titleLarge?.copyWith(
                        fontWeight: FontWeight.w800,
                        fontFeatures: const [FontFeature.tabularFigures()],
                      ),
                    ),
                    if (record.unit != null)
                      Text(record.unit!, style: theme.textTheme.labelSmall),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

String _genderLabel(String gender) => switch (gender) {
      'F' => 'Women',
      'M' => 'Men',
      _ => 'Mixed',
    };
