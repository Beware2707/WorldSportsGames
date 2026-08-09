import 'package:flutter/material.dart';

import '../../domain/event_models.dart';
import '../theme/app_theme.dart';

/// Sport-agnostic result line. The backend's normalized result model
/// (position + value_kind + canonical value_text) renders uniformly for
/// times, distances, points and scores; non-finishers show a status chip.
class ResultTile extends StatelessWidget {
  const ResultTile({super.key, required this.result});

  final EventResult result;

  static const _medals = {
    1: AppColors.medalGold,
    2: AppColors.medalSilver,
    3: AppColors.medalBronze,
  };

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final medal = result.position == null ? null : _medals[result.position];
    final flag = result.athlete.country?.flagEmoji;

    return Card(
      child: ListTile(
        leading: CircleAvatar(
          radius: 16,
          backgroundColor: medal?.withValues(alpha: 0.9) ??
              theme.colorScheme.onSurface.withValues(alpha: 0.08),
          child: Text(
            result.position?.toString() ?? '–',
            style: theme.textTheme.labelLarge?.copyWith(
              color: medal == null ? theme.colorScheme.onSurface : Colors.black87,
            ),
          ),
        ),
        title: Text(result.athlete.fullName,
            style: theme.textTheme.titleSmall
                ?.copyWith(fontWeight: FontWeight.w700)),
        subtitle: Text([
          ?flag,
          result.athlete.country?.iso3 ?? '',
          if (result.lane != null) 'Lane ${result.lane}',
        ].where((s) => s.isNotEmpty).join(' · ')),
        trailing: _trailing(theme),
      ),
    );
  }

  Widget _trailing(ThemeData theme) {
    final status = result.status;
    if (status != null && status != 'ok') {
      return Chip(
        label: Text(status),
        labelStyle: theme.textTheme.labelSmall?.copyWith(
            color: AppColors.live, fontWeight: FontWeight.w800),
        backgroundColor: AppColors.live.withValues(alpha: 0.12),
        visualDensity: VisualDensity.compact,
      );
    }
    return Text(
      result.valueText ?? '—',
      style: theme.textTheme.titleMedium?.copyWith(
        fontWeight: FontWeight.w800,
        fontFeatures: const [FontFeature.tabularFigures()],
      ),
    );
  }
}
