import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/insights_repository.dart';
import '../../domain/insight_models.dart';
import '../../domain/models.dart';
import '../profile/profile_screen.dart';

final notificationsProvider =
    FutureProvider.autoDispose<Paged<AppNotification>>(
  (ref) {
    if (ref.watch(currentUserProvider) == null) {
      return const Paged<AppNotification>(
          items: [], total: 0, page: 1, pages: 1);
    }
    return ref.watch(insightsRepositoryProvider).notifications();
  },
  retry: (count, error) => null,
);

final unreadCountProvider = FutureProvider.autoDispose<int>(
  (ref) async {
    if (ref.watch(currentUserProvider) == null) return 0;
    return ref.watch(insightsRepositoryProvider).unreadCount();
  },
  retry: (count, error) => null,
);

class NotificationsScreen extends ConsumerWidget {
  const NotificationsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final signedIn = ref.watch(currentUserProvider) != null;
    final notifications = ref.watch(notificationsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Notifications'),
        actions: [
          if (signedIn)
            TextButton(
              onPressed: () async {
                final messenger = ScaffoldMessenger.of(context);
                try {
                  await ref.read(insightsRepositoryProvider).markAllRead();
                  ref.invalidate(notificationsProvider);
                  ref.invalidate(unreadCountProvider);
                } on ApiException catch (e) {
                  messenger.showSnackBar(SnackBar(content: Text(e.message)));
                }
              },
              child: const Text('Mark all read'),
            ),
        ],
      ),
      body: !signedIn
          ? const EmptyState(
              icon: Icons.notifications_none_rounded,
              title: 'Sign in to see notifications',
              message: 'Follow athletes and countries to hear about their '
                  'results.',
            )
          : RefreshIndicator(
              onRefresh: () async {
                ref.invalidate(notificationsProvider);
                ref.invalidate(unreadCountProvider);
              },
              child: notifications.when(
                loading: () => const Center(child: CircularProgressIndicator()),
                error: (e, _) => ErrorState(
                  message: e.toString(),
                  onRetry: () => ref.invalidate(notificationsProvider),
                ),
                data: (paged) => paged.items.isEmpty
                    ? ListView(
                        physics: const AlwaysScrollableScrollPhysics(),
                        children: const [
                          SizedBox(height: 120),
                          EmptyState(
                            icon: Icons.notifications_none_rounded,
                            title: 'Nothing yet',
                            message: 'Results for athletes and countries you '
                                'follow will appear here.',
                          ),
                        ],
                      )
                    : ListView.builder(
                        padding: const EdgeInsets.all(AppSpacing.md),
                        itemCount: paged.items.length,
                        itemBuilder: (context, i) =>
                            _NotificationTile(notification: paged.items[i]),
                      ),
              ),
            ),
    );
  }
}

class _NotificationTile extends ConsumerWidget {
  const _NotificationTile({required this.notification});

  final AppNotification notification;

  static const _icons = {
    'medal_result': Icons.workspace_premium_rounded,
    'record_broken': Icons.military_tech_rounded,
    'followed_athlete_result': Icons.timeline_rounded,
    'live_result': Icons.bolt_rounded,
    'breaking_news': Icons.newspaper_rounded,
    'competition_start': Icons.flag_rounded,
    'event_reminder': Icons.alarm_rounded,
  };

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        clipBehavior: Clip.antiAlias,
        color: notification.isUnread
            ? theme.colorScheme.primary.withValues(alpha: 0.06)
            : null,
        child: ListTile(
          leading: Icon(
            _icons[notification.kind] ?? Icons.notifications_rounded,
            color: notification.isUnread ? theme.colorScheme.primary : null,
          ),
          title: Text(
            notification.title,
            style: TextStyle(
              fontWeight:
                  notification.isUnread ? FontWeight.w700 : FontWeight.w400,
            ),
          ),
          subtitle: Text(notification.body),
          trailing: notification.isUnread
              ? const Icon(Icons.circle, size: 10, color: AppColors.primary)
              : null,
          onTap: () async {
            final router = GoRouter.of(context);
            final route = notification.route;
            if (notification.isUnread) {
              try {
                await ref
                    .read(insightsRepositoryProvider)
                    .markRead(notification.id);
                ref.invalidate(notificationsProvider);
                ref.invalidate(unreadCountProvider);
              } on ApiException {
                // Navigation still proceeds — failing to mark read must not
                // block the user from opening what the notification is about.
              }
            }
            if (route != null) router.push(route);
          },
        ),
      ),
    );
  }
}
