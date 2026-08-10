import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../core/widgets/paged_list.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';

final competitionsPageProvider =
    FutureProvider.autoDispose.family<Paged<Competition>, int>(
  (ref, page) =>
      ref.watch(catalogRepositoryProvider).listCompetitions(page: page),
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

class CompetitionsScreen extends ConsumerStatefulWidget {
  const CompetitionsScreen({super.key});

  @override
  ConsumerState<CompetitionsScreen> createState() => _CompetitionsScreenState();
}

class _CompetitionsScreenState extends ConsumerState<CompetitionsScreen> {
  int _page = 1;

  @override
  Widget build(BuildContext context) {
    final competitions = ref.watch(competitionsPageProvider(_page));
    return Scaffold(
      appBar: AppBar(title: const Text('Competitions')),
      body: competitions.when(
        loading: () => const SkeletonList(count: 6, itemHeight: 72),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(competitionsPageProvider(_page)),
        ),
        data: (paged) => paged.items.isEmpty
            ? const EmptyState(
                icon: Icons.emoji_events_outlined, title: 'No competitions yet')
            : Column(
                children: [
                  Expanded(
                    child: ListView.builder(
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
                                _levelLabels[competition.level] ??
                                    competition.level,
                                if (competition.sport != null)
                                  competition.sport!.name,
                              ].join(' · ')),
                              trailing: const Icon(Icons.chevron_right_rounded),
                              // push, not go: go() replaces the branch stack
                              // and leaves the detail with no way back.
                              onTap: () => context
                                  .push('/competitions/${competition.slug}'),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                  PagerBar(
                    page: _page,
                    pages: paged.pages,
                    onPageChanged: (p) => setState(() => _page = p),
                  ),
                ],
              ),
      ),
    );
  }
}
