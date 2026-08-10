import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Page navigation footer shared by paginated list screens.
///
/// Rendered only when more than one page exists, so a short list stays clean
/// while longer ones never silently truncate.
class PagerBar extends StatelessWidget {
  const PagerBar({
    super.key,
    required this.page,
    required this.pages,
    required this.onPageChanged,
  });

  final int page;
  final int pages;
  final ValueChanged<int> onPageChanged;

  @override
  Widget build(BuildContext context) {
    if (pages <= 1) return const SizedBox.shrink();
    return Padding(
      padding: const EdgeInsets.all(AppSpacing.sm),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          IconButton(
            onPressed: page > 1 ? () => onPageChanged(page - 1) : null,
            icon: const Icon(Icons.chevron_left_rounded),
            tooltip: 'Previous page',
          ),
          Text('Page $page of $pages'),
          IconButton(
            onPressed: page < pages ? () => onPageChanged(page + 1) : null,
            icon: const Icon(Icons.chevron_right_rounded),
            tooltip: 'Next page',
          ),
        ],
      ),
    );
  }
}

/// Vertical skeleton placeholder list used while a page loads.
class SkeletonList extends StatelessWidget {
  const SkeletonList({super.key, this.count = 8, this.itemHeight = 64});

  final int count;
  final double itemHeight;

  @override
  Widget build(BuildContext context) {
    return ListView.builder(
      padding: const EdgeInsets.all(AppSpacing.md),
      itemCount: count,
      itemBuilder: (_, _) => Padding(
        padding: const EdgeInsets.only(bottom: AppSpacing.sm),
        child: _SkeletonRow(height: itemHeight),
      ),
    );
  }
}

class _SkeletonRow extends StatelessWidget {
  const _SkeletonRow({required this.height});

  final double height;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: height,
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.onSurface.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(AppSpacing.cardRadius),
      ),
    );
  }
}
