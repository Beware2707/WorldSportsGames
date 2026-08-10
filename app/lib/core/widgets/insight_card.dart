import 'package:flutter/material.dart';

import '../../domain/insight_models.dart';
import '../theme/app_theme.dart';

/// The only widget that renders generated text.
///
/// Routing every insight through here means an insight can never appear
/// without its disclaimer, and estimates are visually distinct from derived
/// summaries — a confident-sounding sentence is still a guess.
class InsightCard extends StatelessWidget {
  const InsightCard({super.key, required this.insight, this.title});

  final AIInsight insight;
  final String? title;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final accent =
        insight.isEstimate ? AppColors.energy : theme.colorScheme.primary;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AppSpacing.lg),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  insight.isEstimate
                      ? Icons.query_stats_rounded
                      : Icons.auto_awesome_rounded,
                  size: 18,
                  color: accent,
                ),
                const SizedBox(width: AppSpacing.sm),
                Expanded(
                  child: Text(
                    title ?? (insight.isEstimate ? 'Estimate' : 'Summary'),
                    style: theme.textTheme.labelLarge?.copyWith(color: accent),
                  ),
                ),
                // The label is part of the content, not decoration: it states
                // plainly what kind of claim this is.
                Chip(
                  label: Text(insight.isEstimate ? 'ESTIMATE' : 'AUTO-GENERATED'),
                  labelStyle: theme.textTheme.labelSmall
                      ?.copyWith(fontWeight: FontWeight.w800, color: accent),
                  backgroundColor: accent.withValues(alpha: 0.12),
                  visualDensity: VisualDensity.compact,
                  side: BorderSide.none,
                ),
              ],
            ),
            const SizedBox(height: AppSpacing.md),
            Text(insight.text, style: theme.textTheme.bodyMedium),
            const SizedBox(height: AppSpacing.md),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(Icons.info_outline_rounded,
                    size: 14, color: theme.secondaryText),
                const SizedBox(width: AppSpacing.xs),
                Expanded(
                  child: Text(
                    insight.disclaimer,
                    style: theme.textTheme.bodySmall
                        ?.copyWith(color: theme.secondaryText),
                  ),
                ),
              ],
            ),
            if (insight.basis.isNotEmpty) ...[
              const SizedBox(height: AppSpacing.xs),
              Text(
                'Based on: ${insight.basis.join(", ")} · confidence: '
                '${insight.confidence}',
                style: theme.textTheme.labelSmall
                    ?.copyWith(color: theme.tertiaryText),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
