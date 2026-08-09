import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Pulsing dot + LIVE label. Rendered ONLY for data whose status is genuinely
/// `live` from the backend — never for estimates or decoration.
class LiveBadge extends StatefulWidget {
  const LiveBadge({super.key});

  @override
  State<LiveBadge> createState() => _LiveBadgeState();
}

class _LiveBadgeState extends State<LiveBadge>
    with SingleTickerProviderStateMixin {
  late final AnimationController _pulse = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1200),
  )..repeat(reverse: true);

  @override
  void dispose() {
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: AppColors.live.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(AppSpacing.chipRadius),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          FadeTransition(
            opacity: Tween(begin: 0.4, end: 1.0).animate(_pulse),
            child: const CircleAvatar(radius: 4, backgroundColor: AppColors.live),
          ),
          const SizedBox(width: 6),
          Text(
            'LIVE',
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
                  color: AppColors.live,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 1,
                ),
          ),
        ],
      ),
    );
  }
}

class SectionHeader extends StatelessWidget {
  const SectionHeader(this.title, {super.key, this.trailing});

  final String title;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(
          AppSpacing.md, AppSpacing.lg, AppSpacing.md, AppSpacing.sm),
      child: Row(
        children: [
          Expanded(
            child: Text(title, style: Theme.of(context).textTheme.titleLarge),
          ),
          ?trailing,
        ],
      ),
    );
  }
}

/// Shimmer-free skeleton block (honest loading state, cheap to render).
class SkeletonBox extends StatelessWidget {
  const SkeletonBox({super.key, this.height = 16, this.width, this.radius = 8});

  final double height;
  final double? width;
  final double radius;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: height,
      width: width,
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.onSurface.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(radius),
      ),
    );
  }
}

class EmptyState extends StatelessWidget {
  const EmptyState({
    super.key,
    required this.icon,
    required this.title,
    this.message,
  });

  final IconData icon;
  final String title;
  final String? message;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(AppSpacing.xl),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon,
                size: 56,
                color: theme.colorScheme.onSurface.withValues(alpha: 0.3)),
            const SizedBox(height: AppSpacing.md),
            Text(title,
                style: theme.textTheme.titleMedium, textAlign: TextAlign.center),
            if (message != null) ...[
              const SizedBox(height: AppSpacing.sm),
              Text(
                message!,
                style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.onSurface.withValues(alpha: 0.6)),
                textAlign: TextAlign.center,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class ErrorState extends StatelessWidget {
  const ErrorState({super.key, required this.message, required this.onRetry});

  final String message;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(AppSpacing.xl),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.cloud_off_rounded, size: 56),
            const SizedBox(height: AppSpacing.md),
            Text(message, textAlign: TextAlign.center),
            const SizedBox(height: AppSpacing.md),
            FilledButton.icon(
              onPressed: onRetry,
              icon: const Icon(Icons.refresh_rounded),
              label: const Text('Try again'),
            ),
          ],
        ),
      ),
    );
  }
}

/// Maps backend icon names (Material Symbols) to Flutter icons with a
/// sport-neutral fallback.
IconData sportIcon(String? name) => switch (name) {
      'pool' => Icons.pool_rounded,
      'sprint' => Icons.directions_run_rounded,
      'sports_basketball' => Icons.sports_basketball_rounded,
      'sports_soccer' => Icons.sports_soccer_rounded,
      'sports_tennis' => Icons.sports_tennis_rounded,
      'sports_cricket' => Icons.sports_cricket_rounded,
      'sports_golf' => Icons.sports_golf_rounded,
      'sports_volleyball' => Icons.sports_volleyball_rounded,
      'sports_handball' => Icons.sports_handball_rounded,
      'sports_hockey' => Icons.sports_hockey_rounded,
      'sports_mma' => Icons.sports_mma_rounded,
      'directions_bike' => Icons.directions_bike_rounded,
      'rowing' => Icons.rowing_rounded,
      'sailing' => Icons.sailing_rounded,
      'downhill_skiing' => Icons.downhill_skiing_rounded,
      'snowboarding' => Icons.snowboarding_rounded,
      'ice_skating' => Icons.ice_skating_rounded,
      'surfing' => Icons.surfing_rounded,
      'skateboarding' => Icons.skateboarding_rounded,
      'hiking' => Icons.hiking_rounded,
      _ => Icons.emoji_events_rounded,
    };
