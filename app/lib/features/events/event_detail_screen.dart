import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../core/widgets/result_tile.dart';
import '../../data/live_repository.dart';
import '../../domain/event_models.dart';

final eventDetailProvider =
    FutureProvider.autoDispose.family<SportEventDetail, int>(
  (ref, id) => ref.watch(eventsRepositoryProvider).eventDetail(id),
  retry: (count, error) => null,
);

class EventDetailScreen extends ConsumerWidget {
  const EventDetailScreen({super.key, required this.eventId});

  final int eventId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final detail = ref.watch(eventDetailProvider(eventId));
    return Scaffold(
      appBar: AppBar(title: Text(detail.value?.name ?? '')),
      body: detail.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(eventDetailProvider(eventId)),
        ),
        data: (event) => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(AppSpacing.lg),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Expanded(
                          child: Text(event.competitionName,
                              style: Theme.of(context).textTheme.titleMedium),
                        ),
                        if (event.isLive) const LiveBadge(),
                      ],
                    ),
                    const SizedBox(height: AppSpacing.xs),
                    Text(
                      [
                        if (event.edition != null) event.edition!.label,
                        if (event.discipline != null) event.discipline!.name,
                        event.phase,
                      ].join(' · '),
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ],
                ),
              ),
            ),
            const SectionHeader('Results'),
            if (event.results.isEmpty)
              EmptyState(
                icon: Icons.hourglass_empty_rounded,
                title: event.status == 'scheduled'
                    ? 'Not started yet'
                    : 'No results recorded',
                message: event.status == 'scheduled'
                    ? 'Results appear here once the event is underway.'
                    : null,
              ),
            for (final result in event.results)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                child: ResultTile(result: result),
              ),
          ],
        ),
      ),
    );
  }
}
