import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';

/// Paged athlete list. retry: null — errors surface with manual retry.
final athletesPageProvider =
    FutureProvider.autoDispose.family<Paged<Athlete>, int>(
  (ref, page) => ref.watch(catalogRepositoryProvider).listAthletes(page: page),
  retry: (count, error) => null,
);

class AthletesScreen extends ConsumerStatefulWidget {
  const AthletesScreen({super.key});

  @override
  ConsumerState<AthletesScreen> createState() => _AthletesScreenState();
}

class _AthletesScreenState extends ConsumerState<AthletesScreen> {
  int _page = 1;

  @override
  Widget build(BuildContext context) {
    final athletes = ref.watch(athletesPageProvider(_page));
    return Scaffold(
      appBar: AppBar(title: const Text('Athletes')),
      body: athletes.when(
        loading: () => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: List.generate(
            8,
            (_) => const Padding(
              padding: EdgeInsets.only(bottom: AppSpacing.sm),
              child: SkeletonBox(height: 64, radius: AppSpacing.cardRadius),
            ),
          ),
        ),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(athletesPageProvider(_page)),
        ),
        data: (paged) => paged.items.isEmpty
            ? const EmptyState(
                icon: Icons.person_search_rounded, title: 'No athletes yet')
            : Column(
                children: [
                  Expanded(
                    child: ListView.builder(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      itemCount: paged.items.length,
                      itemBuilder: (context, i) {
                        final athlete = paged.items[i];
                        return Padding(
                          padding:
                              const EdgeInsets.only(bottom: AppSpacing.sm),
                          child: Card(
                            child: ListTile(
                              leading: CircleAvatar(
                                child: Text(athlete.fullName.isEmpty
                                    ? '?'
                                    : athlete.fullName[0]),
                              ),
                              title: Text(athlete.fullName),
                              subtitle: Text([
                                athlete.country?.flagEmoji,
                                athlete.country?.name,
                              ].whereType<String>().join(' ')),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                  if (paged.pages > 1)
                    Padding(
                      padding: const EdgeInsets.all(AppSpacing.sm),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          IconButton(
                            onPressed: _page > 1
                                ? () => setState(() => _page--)
                                : null,
                            icon: const Icon(Icons.chevron_left_rounded),
                          ),
                          Text('Page $_page of ${paged.pages}'),
                          IconButton(
                            onPressed: _page < paged.pages
                                ? () => setState(() => _page++)
                                : null,
                            icon: const Icon(Icons.chevron_right_rounded),
                          ),
                        ],
                      ),
                    ),
                ],
              ),
      ),
    );
  }
}
