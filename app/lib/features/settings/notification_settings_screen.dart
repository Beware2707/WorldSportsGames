import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/personalization_repository.dart';
import '../../domain/personalization_models.dart';

final notificationSettingsProvider =
    FutureProvider.autoDispose<List<NotificationSetting>>(
  (ref) => ref.watch(personalizationRepositoryProvider).notificationSettings(),
  retry: (count, error) => null,
);

class NotificationSettingsScreen extends ConsumerStatefulWidget {
  const NotificationSettingsScreen({super.key});

  @override
  ConsumerState<NotificationSettingsScreen> createState() =>
      _NotificationSettingsScreenState();
}

class _NotificationSettingsScreenState
    extends ConsumerState<NotificationSettingsScreen> {
  bool _saving = false;

  Future<void> _toggle(List<NotificationSetting> current, String kind, bool value) async {
    setState(() => _saving = true);
    final messenger = ScaffoldMessenger.of(context);
    try {
      final updated = await ref
          .read(personalizationRepositoryProvider)
          .updateNotificationSettings(
            [NotificationSetting(kind: kind, enabled: value)],
          );
      if (!mounted) return;
      // Server response is authoritative — never assume the write landed.
      ref.invalidate(notificationSettingsProvider);
      if (updated.isEmpty) {
        messenger.showSnackBar(
          const SnackBar(content: Text('Could not save that preference')),
        );
      }
    } on ApiException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final settings = ref.watch(notificationSettingsProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Notifications')),
      body: settings.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(notificationSettingsProvider),
        ),
        data: (items) => ListView(
          children: [
            Padding(
              padding: const EdgeInsets.all(AppSpacing.md),
              child: Text(
                'Choose what you hear about. Delivery arrives in a later '
                'release — these preferences are saved now and honoured then.',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ),
            for (final setting in items)
              SwitchListTile(
                title: Text(setting.label),
                value: setting.enabled,
                onChanged: _saving
                    ? null
                    : (value) => _toggle(items, setting.kind, value),
              ),
          ],
        ),
      ),
    );
  }
}
