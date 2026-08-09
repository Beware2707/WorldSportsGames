import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/live_repository.dart';
import '../../domain/event_models.dart';

/// Live Center state: REST list as the authoritative base, WebSocket diffs
/// layered on top. Shows ONLY events with genuine live coverage — there is
/// no simulated or client-side-invented live data path.
class LiveCenterController extends AsyncNotifier<List<LiveCoverage>> {
  StreamSubscription<LiveSocketMessage>? _subscription;

  @override
  Future<List<LiveCoverage>> build() async {
    final socket = ref.watch(liveSocketProvider);
    _subscription = socket.connect().listen(
          _onMessage,
          // A dropped socket must not break the screen; REST refresh covers it.
          onError: (_) {},
        );
    ref.onDispose(() => _subscription?.cancel());
    return ref.watch(eventsRepositoryProvider).liveEvents();
  }

  Future<void> refresh() async {
    state = AsyncData(await ref.read(eventsRepositoryProvider).liveEvents());
  }

  Future<void> _onMessage(LiveSocketMessage message) async {
    switch (message) {
      case LiveSnapshot(:final events):
        state = AsyncData(events);
      case LiveDiff(:final frame):
        if (frame.kind == 'status') {
          // Events entering/leaving live coverage → refetch the full list.
          await refresh();
        } else {
          final current = state.value;
          if (current == null) return;
          state = AsyncData([
            for (final coverage in current)
              if (coverage.event.id == frame.eventId)
                LiveCoverage(
                  event: coverage.event,
                  editionLabel: coverage.editionLabel,
                  competitionName: coverage.competitionName,
                  competitionSlug: coverage.competitionSlug,
                  currentPhase:
                      frame.payload['phase'] as String? ?? coverage.currentPhase,
                  lastSeq: frame.seq,
                )
              else
                coverage,
          ]);
        }
    }
  }
}

final liveCenterProvider =
    AsyncNotifierProvider.autoDispose<LiveCenterController, List<LiveCoverage>>(
        LiveCenterController.new, retry: (count, error) => null);

class LiveScreen extends ConsumerWidget {
  const LiveScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final live = ref.watch(liveCenterProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Live')),
      body: RefreshIndicator(
        onRefresh: () => ref.read(liveCenterProvider.notifier).refresh(),
        child: live.when(
          loading: () => const Center(child: CircularProgressIndicator()),
          error: (e, _) => ErrorState(
            message: e.toString(),
            onRetry: () => ref.invalidate(liveCenterProvider),
          ),
          data: (events) => events.isEmpty
              ? ListView(
                  physics: const AlwaysScrollableScrollPhysics(),
                  children: const [
                    SizedBox(height: 120),
                    EmptyState(
                      icon: Icons.sensors_rounded,
                      title: 'Nothing is live right now',
                      message:
                          'Events appear here the moment genuine live coverage '
                          'starts — never simulated.',
                    ),
                  ],
                )
              : ListView.builder(
                  physics: const AlwaysScrollableScrollPhysics(),
                  padding: const EdgeInsets.all(AppSpacing.md),
                  itemCount: events.length,
                  itemBuilder: (context, i) =>
                      _LiveCard(coverage: events[i]),
                ),
        ),
      ),
    );
  }
}

class _LiveCard extends StatelessWidget {
  const _LiveCard({required this.coverage});

  final LiveCoverage coverage;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(coverage.competitionName,
                        style: theme.textTheme.bodySmall),
                  ),
                  const LiveBadge(),
                ],
              ),
              const SizedBox(height: AppSpacing.xs),
              Text(coverage.event.name, style: theme.textTheme.titleMedium),
              const SizedBox(height: AppSpacing.xs),
              Text(
                [
                  coverage.editionLabel,
                  if (coverage.currentPhase != null) coverage.currentPhase!,
                  'update #${coverage.lastSeq}',
                ].join(' · '),
                style: theme.textTheme.bodySmall?.copyWith(
                    color: theme.colorScheme.onSurface.withValues(alpha: 0.6)),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
