import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../core/widgets/paged_list.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follow_button.dart';
import '../search/search_screen.dart';

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
      appBar: AppBar(
        title: const Text('Athletes'),
        actions: const [SearchIconButton()],
      ),
      body: athletes.when(
        loading: () => const SkeletonList(),
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
                          padding: const EdgeInsets.only(bottom: AppSpacing.sm),
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
                              trailing: FollowButton(
                                kind: FollowKind.athlete,
                                entityId: athlete.id,
                                name: athlete.fullName,
                                compact: true,
                              ),
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
