import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/live_repository.dart';
import '../../domain/event_models.dart';

/// Whether the live stream is currently delivering updates.
enum LiveStreamStatus { connecting, streaming, disconnected }

class LiveCenterState {
  const LiveCenterState({required this.events, required this.streamStatus});

  final List<LiveCoverage> events;
  final LiveStreamStatus streamStatus;

  /// LIVE may only be presented while updates are genuinely flowing. If the
  /// socket is down we still show the events, but explicitly as "last known",
  /// never as live — a frozen snapshot is not live coverage.
  bool get canPresentAsLive => streamStatus == LiveStreamStatus.streaming;

  LiveCenterState copyWith({
    List<LiveCoverage>? events,
    LiveStreamStatus? streamStatus,
  }) =>
      LiveCenterState(
        events: events ?? this.events,
        streamStatus: streamStatus ?? this.streamStatus,
      );
}

/// Live Center state: REST list as the authoritative base, WebSocket diffs
/// layered on top. Shows ONLY events with genuine live coverage — there is
/// no simulated or client-side-invented live data path.
class LiveCenterController extends AsyncNotifier<LiveCenterState> {
  StreamSubscription<LiveSocketMessage>? _subscription;
  Timer? _reconnectTimer;
  int _attempt = 0;

  @override
  Future<LiveCenterState> build() async {
    // Captured here: Ref must not be used inside onDispose callbacks.
    final socket = ref.watch(liveSocketProvider);
    ref.onDispose(() {
      _reconnectTimer?.cancel();
      _subscription?.cancel();
      socket.close(); // cancelling the subscription alone leaks the socket
    });
    _listen(socket);
    final events = await ref.watch(eventsRepositoryProvider).liveEvents();
    return LiveCenterState(
      events: events,
      streamStatus: LiveStreamStatus.connecting,
    );
  }

  void _listen(LiveSocket socket) {
    _subscription?.cancel();
    _subscription = socket.connect().listen(_onMessage);
  }

  /// Refetch the authoritative list. Failures degrade to disconnected rather
  /// than throwing into the void — the UI must know the data may be stale.
  Future<void> refresh() async {
    try {
      final events = await ref.read(eventsRepositoryProvider).liveEvents();
      if (!ref.mounted) return;
      state = AsyncData(
        (state.value ??
                const LiveCenterState(
                    events: [], streamStatus: LiveStreamStatus.connecting))
            .copyWith(events: events),
      );
    } catch (_) {
      if (!ref.mounted) return;
      final current = state.value;
      if (current != null) {
        state = AsyncData(
            current.copyWith(streamStatus: LiveStreamStatus.disconnected));
      }
    }
  }

  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    final seconds = [2, 5, 10, 20, 30][_attempt.clamp(0, 4)];
    _attempt++;
    _reconnectTimer = Timer(Duration(seconds: seconds), () {
      if (!ref.mounted) return;
      _listen(ref.read(liveSocketProvider));
      refresh();
    });
  }

  Future<void> _onMessage(LiveSocketMessage message) async {
    final current = state.value;
    switch (message) {
      case LiveSnapshot(:final events):
        _attempt = 0;
        state = AsyncData(LiveCenterState(
          events: events,
          streamStatus: LiveStreamStatus.streaming,
        ));
      case LiveDisconnected():
        // Stop presenting anything as live and start backing off.
        if (current != null) {
          state = AsyncData(
              current.copyWith(streamStatus: LiveStreamStatus.disconnected));
        }
        _scheduleReconnect();
      case LiveDiff(:final frame):
        if (frame.kind == 'status') {
          // Events entering/leaving live coverage → refetch the full list.
          await refresh();
        } else {
          if (current == null) return;
          state = AsyncData(current.copyWith(
            streamStatus: LiveStreamStatus.streaming,
            events: [
              for (final coverage in current.events)
                if (coverage.event.id == frame.eventId)
                  LiveCoverage(
                    event: coverage.event,
                    editionLabel: coverage.editionLabel,
                    competitionName: coverage.competitionName,
                    competitionSlug: coverage.competitionSlug,
                    currentPhase: frame.payload['phase'] as String? ??
                        coverage.currentPhase,
                    lastSeq: frame.seq,
                  )
                else
                  coverage,
            ],
          ));
        }
    }
  }
}

final liveCenterProvider =
    AsyncNotifierProvider.autoDispose<LiveCenterController, LiveCenterState>(
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
          data: (data) => data.events.isEmpty
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
              : ListView(
                  physics: const AlwaysScrollableScrollPhysics(),
                  padding: const EdgeInsets.all(AppSpacing.md),
                  children: [
                    if (!data.canPresentAsLive) const _StaleBanner(),
                    for (final coverage in data.events)
                      _LiveCard(
                        coverage: coverage,
                        isLive: data.canPresentAsLive,
                      ),
                  ],
                ),
        ),
      ),
    );
  }
}

/// Shown whenever updates are not flowing, so a frozen list is never mistaken
/// for live coverage.
class _StaleBanner extends StatelessWidget {
  const _StaleBanner();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        color: theme.colorScheme.tertiary.withValues(alpha: 0.12),
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Row(
            children: [
              Icon(Icons.cloud_off_rounded, color: theme.colorScheme.tertiary),
              const SizedBox(width: AppSpacing.md),
              const Expanded(
                child: Text(
                  'Live updates are paused — reconnecting. The results below '
                  'are the last known state, not live.',
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _LiveCard extends StatelessWidget {
  const _LiveCard({required this.coverage, required this.isLive});

  final LiveCoverage coverage;
  final bool isLive;

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
                  if (isLive)
                    const LiveBadge()
                  else
                    Chip(
                      label: const Text('LAST KNOWN'),
                      labelStyle: theme.textTheme.labelSmall
                          ?.copyWith(fontWeight: FontWeight.w800),
                      visualDensity: VisualDensity.compact,
                    ),
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
                    color: theme.secondaryText),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
