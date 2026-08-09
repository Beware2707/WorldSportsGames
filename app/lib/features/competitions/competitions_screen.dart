import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';

final competitionsProvider = FutureProvider.autoDispose<Paged<Competition>>(
  (ref) => ref.watch(catalogRepositoryProvider).listCompetitions(),
  retry: (count, error) => null,
);

const _levelLabels = {
  'olympic': 'Olympic',
  'world': 'World',
  'continental': 'Continental',
  'league': 'League',
  'national': 'National',
  'other': 'Other',
};

class CompetitionsScreen extends ConsumerWidget {
  const CompetitionsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final competitions = ref.watch(competitionsProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Competitions')),
      body: competitions.when(
        loading: () => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: List.generate(
            6,
            (_) => const Padding(
              padding: EdgeInsets.only(bottom: AppSpacing.sm),
              child: SkeletonBox(height: 72, radius: AppSpacing.cardRadius),
            ),
          ),
        ),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(competitionsProvider),
        ),
        data: (paged) => paged.items.isEmpty
            ? const EmptyState(
                icon: Icons.emoji_events_outlined, title: 'No competitions yet')
            : ListView.builder(
                padding: const EdgeInsets.all(AppSpacing.md),
                itemCount: paged.items.length,
                itemBuilder: (context, i) {
                  final competition = paged.items[i];
                  return Padding(
                    padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                    child: Card(
                      clipBehavior: Clip.antiAlias,
                      child: ListTile(
                        leading: Icon(
                          sportIcon(competition.sport?.icon),
                          color: Theme.of(context).colorScheme.primary,
                        ),
                        title: Text(competition.name),
                        subtitle: Text([
                          _levelLabels[competition.level] ?? competition.level,
                          if (competition.sport != null)
                            competition.sport!.name,
                        ].join(' · ')),
                        trailing: const Icon(Icons.chevron_right_rounded),
                        onTap: () =>
                            context.go('/competitions/${competition.slug}'),
                      ),
                    ),
                  );
                },
              ),
      ),
    );
  }
}
