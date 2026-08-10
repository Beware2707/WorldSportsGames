import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/live_repository.dart';
import '../../domain/event_models.dart';

final editionEventsProvider =
    FutureProvider.autoDispose.family<List<SportEvent>, int>(
  (ref, editionId) =>
      ref.watch(eventsRepositoryProvider).allEventsForEdition(editionId),
  retry: (count, error) => null,
);

class EditionEventsScreen extends ConsumerWidget {
  const EditionEventsScreen({super.key, required this.editionId});

  final int editionId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final events = ref.watch(editionEventsProvider(editionId));
    return Scaffold(
      appBar: AppBar(title: const Text('Schedule & Results')),
      body: events.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(editionEventsProvider(editionId)),
        ),
        data: (events) => events.isEmpty
            ? const EmptyState(
                icon: Icons.event_note_rounded,
                title: 'No events published yet',
                message:
                    'The schedule appears here once events are announced.',
              )
            : ListView.builder(
                padding: const EdgeInsets.all(AppSpacing.md),
                itemCount: events.length,
                itemBuilder: (context, i) {
                  final event = events[i];
                  return Padding(
                    padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                    child: Card(
                      clipBehavior: Clip.antiAlias,
                      child: ListTile(
                        title: Text(event.name),
                        subtitle: Text([
                          if (event.discipline != null) event.discipline!.name,
                          event.phase,
                          if (event.scheduledStart != null)
                            MaterialLocalizations.of(context).formatShortDate(
                                event.scheduledStart!.toLocal()),
                        ].join(' · ')),
                        trailing: event.isLive
                            ? const LiveBadge()
                            : Icon(
                                switch (event.status) {
                                  'completed' => Icons.flag_rounded,
                                  'cancelled' => Icons.cancel_outlined,
                                  _ => Icons.schedule_rounded,
                                },
                                color: Theme.of(context)
                                    .colorScheme
                                    .onSurface
                                    .withValues(alpha: 0.5),
                              ),
                        onTap: () => context.push(
                            '${GoRouterState.of(context).matchedLocation}'
                            '/events/${event.id}'),
                      ),
                    ),
                  );
                },
              ),
      ),
    );
  }
}
