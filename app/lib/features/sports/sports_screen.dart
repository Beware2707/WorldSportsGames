import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../domain/models.dart';
import 'sports_providers.dart';

const _categories = <(String?, String)>[
  (null, 'All'),
  ('summer', 'Summer'),
  ('winter', 'Winter'),
  ('la28', 'LA28'),
];

class SportsScreen extends ConsumerStatefulWidget {
  const SportsScreen({super.key});

  @override
  ConsumerState<SportsScreen> createState() => _SportsScreenState();
}

class _SportsScreenState extends ConsumerState<SportsScreen> {
  String? _category;

  @override
  Widget build(BuildContext context) {
    final sports = ref.watch(sportsProvider(_category));
    return Scaffold(
      appBar: AppBar(title: const Text('Sports')),
      body: Column(
        children: [
          SizedBox(
            height: 48,
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
              itemCount: _categories.length,
              separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
              itemBuilder: (context, i) {
                final (value, label) = _categories[i];
                return FilterChip(
                  label: Text(label),
                  selected: _category == value,
                  onSelected: (_) => setState(() => _category = value),
                );
              },
            ),
          ),
          Expanded(
            child: sports.when(
              loading: () => GridView.builder(
                padding: const EdgeInsets.all(AppSpacing.md),
                gridDelegate: _gridDelegate,
                itemCount: 12,
                itemBuilder: (_, _) =>
                    const SkeletonBox(radius: AppSpacing.cardRadius),
              ),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(sportsProvider(_category)),
              ),
              data: (items) => items.isEmpty
                  ? const EmptyState(
                      icon: Icons.emoji_events_outlined,
                      title: 'No sports found')
                  : GridView.builder(
                      padding: const EdgeInsets.all(AppSpacing.md),
                      gridDelegate: _gridDelegate,
                      itemCount: items.length,
                      itemBuilder: (context, i) => _SportCard(sport: items[i]),
                    ),
            ),
          ),
        ],
      ),
    );
  }
}

const _gridDelegate = SliverGridDelegateWithMaxCrossAxisExtent(
  maxCrossAxisExtent: 200,
  mainAxisSpacing: AppSpacing.sm,
  crossAxisSpacing: AppSpacing.sm,
  childAspectRatio: 1.45,
);

class _SportCard extends StatelessWidget {
  const _SportCard({required this.sport});

  final Sport sport;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () => context.go('/sports/${sport.code}'),
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(sportIcon(sport.icon),
                  size: 26, color: theme.colorScheme.primary),
              const Spacer(),
              Text(sport.name,
                  style: theme.textTheme.titleSmall
                      ?.copyWith(fontWeight: FontWeight.w700),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis),
            ],
          ),
        ),
      ),
    );
  }
}
