import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import 'sports_providers.dart';

class SportDetailScreen extends ConsumerWidget {
  const SportDetailScreen({super.key, required this.code});

  final String code;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final sport = ref.watch(sportDetailProvider(code));
    return Scaffold(
      appBar: AppBar(title: Text(sport.value?.name ?? '')),
      body: sport.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(sportDetailProvider(code)),
        ),
        data: (s) => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(AppSpacing.lg),
                child: Row(
                  children: [
                    Icon(sportIcon(s.icon),
                        size: 44, color: Theme.of(context).colorScheme.primary),
                    const SizedBox(width: AppSpacing.md),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(s.name,
                              style: Theme.of(context).textTheme.titleLarge),
                          const SizedBox(height: AppSpacing.xs),
                          Chip(
                            label: Text(switch (s.category) {
                              'summer' => 'Summer Games',
                              'winter' => 'Winter Games',
                              _ => 'LA28 Addition',
                            }),
                            visualDensity: VisualDensity.compact,
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SectionHeader('Disciplines'),
            for (final d in s.disciplines)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                child: Card(
                  child: ListTile(
                    leading: const Icon(Icons.flag_circle_outlined),
                    title: Text(d.name),
                    // Discipline detail (events, rankings, records) — Sprint 2+.
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
