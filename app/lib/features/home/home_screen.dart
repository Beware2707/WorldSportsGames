import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../domain/models.dart';
import '../search/search_screen.dart';
import 'home_providers.dart';

class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final feed = ref.watch(homeFeedProvider);
    return Scaffold(
      appBar: AppBar(
        title: const Text('World Sports'),
        actions: const [SearchIconButton()],
      ),
      body: RefreshIndicator(
        onRefresh: () => ref.refresh(homeFeedProvider.future),
        child: feed.when(
          loading: () => const _HomeSkeleton(),
          error: (e, _) => ErrorState(
            message: e.toString(),
            onRetry: () => ref.invalidate(homeFeedProvider),
          ),
          data: (sections) => ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.only(bottom: AppSpacing.xl),
            children: [
              const _DiscoverRow(),
              for (final section in sections) _HomeSectionView(section: section),
            ],
          ),
        ),
      ),
    );
  }
}

/// Entry points to the reference sections that aren't bottom-nav tabs.
class _DiscoverRow extends StatelessWidget {
  const _DiscoverRow();

  static const _items = <(IconData, String, String)>[
    (Icons.workspace_premium_rounded, 'Medals', '/medals'),
    (Icons.leaderboard_rounded, 'Rankings', '/rankings'),
    (Icons.military_tech_rounded, 'Records', '/records'),
    (Icons.person_search_rounded, 'Athletes', '/home/athletes'),
  ];

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return SizedBox(
      height: 104,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.fromLTRB(
            AppSpacing.md, AppSpacing.md, AppSpacing.md, AppSpacing.sm),
        itemCount: _items.length,
        separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
        itemBuilder: (context, i) {
          final (icon, label, route) = _items[i];
          return SizedBox(
            width: 104,
            child: Card(
              clipBehavior: Clip.antiAlias,
              child: InkWell(
                onTap: () => GoRouter.of(context).push(route),
                child: Padding(
                  padding: const EdgeInsets.all(AppSpacing.sm),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(icon, color: theme.colorScheme.primary),
                      const SizedBox(height: AppSpacing.xs),
                      Flexible(
                        child: Text(label,
                            style: theme.textTheme.labelLarge,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}

class _HomeSectionView extends StatelessWidget {
  const _HomeSectionView({required this.section});

  final HomeSection section;

  @override
  Widget build(BuildContext context) {
    final body = switch (section.kind) {
      'live_now' => _liveNow(context),
      'up_next' || 'featured_competitions' || 'following_schedule' =>
        _cardsRow(context),
      'athlete_spotlight' || 'your_athletes' => _athleteRow(context),
      'your_sports' => _sportRow(context),
      // Unknown kinds from newer servers degrade gracefully to nothing.
      _ => const SizedBox.shrink(),
    };
    if (section.kind == 'live_now' && section.items.isNotEmpty) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [SectionHeader(section.title), body],
      );
    }
    if (section.kind != 'live_now' && section.items.isEmpty) {
      return const SizedBox.shrink();
    }
    final seeAllRoute = switch (section.kind) {
      'featured_competitions' || 'up_next' || 'following_schedule' =>
        '/home/competitions',
      'athlete_spotlight' || 'your_athletes' => '/home/athletes',
      'your_sports' => '/sports',
      _ => null,
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SectionHeader(
          section.title,
          trailing: seeAllRoute == null
              ? null
              : TextButton(
                  onPressed: () => GoRouter.of(context).go(seeAllRoute),
                  child: const Text('See all'),
                ),
        ),
        body,
      ],
    );
  }

  Widget _liveNow(BuildContext context) {
    if (section.items.isEmpty) {
      return Padding(
        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
        child: Card(
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.md),
            child: Row(
              children: [
                Icon(Icons.sensors_off_rounded,
                    color: Theme.of(context)
                        .colorScheme
                        .onSurface
                        .withValues(alpha: 0.4)),
                const SizedBox(width: AppSpacing.md),
                const Expanded(
                  child: Text('No events are live right now. '
                      'Live coverage appears here the moment it starts.'),
                ),
              ],
            ),
          ),
        ),
      );
    }
    // live_now items are LiveCoverage payloads (event + competition), the
    // same shape the Live Center consumes — sourced from real live coverage.
    return SizedBox(
      height: 148,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
        itemCount: section.items.length,
        separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
        itemBuilder: (context, i) {
          final item = section.items[i];
          return _LiveNowCard(
            eventName:
                (item['event'] as Map<String, dynamic>)['name'] as String,
            competitionName: item['competition_name'] as String,
            editionLabel: item['edition_label'] as String,
          );
        },
      ),
    );
  }

  Widget _cardsRow(BuildContext context) {
    return SizedBox(
      height: 148,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
        itemCount: section.items.length,
        separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
        itemBuilder: (context, i) {
          final item = section.items[i];
          final isEdition = item.containsKey('label');
          final edition = isEdition ? Edition.fromJson(item) : null;
          final competition =
              edition?.competition ?? Competition.fromJson(item);
          return _EventCard(edition: edition, competition: competition);
        },
      ),
    );
  }

  Widget _sportRow(BuildContext context) {
    return SizedBox(
      height: 96,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
        itemCount: section.items.length,
        separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
        itemBuilder: (context, i) {
          final sport = Sport.fromJson(section.items[i]);
          return SizedBox(
            width: 150,
            child: Card(
              clipBehavior: Clip.antiAlias,
              child: InkWell(
                onTap: () => GoRouter.of(context).go('/sports/${sport.code}'),
                child: Padding(
                  padding: const EdgeInsets.all(AppSpacing.md),
                  child: Row(
                    children: [
                      Icon(sportIcon(sport.icon),
                          color: Theme.of(context).colorScheme.primary),
                      const SizedBox(width: AppSpacing.sm),
                      Expanded(
                        child: Text(sport.name,
                            style: Theme.of(context).textTheme.titleSmall,
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          );
        },
      ),
    );
  }

  Widget _athleteRow(BuildContext context) {
    return SizedBox(
      height: 152,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
        itemCount: section.items.length,
        separatorBuilder: (_, _) => const SizedBox(width: AppSpacing.sm),
        itemBuilder: (context, i) {
          final athlete = Athlete.fromJson(section.items[i]);
          return _AthleteCard(athlete: athlete);
        },
      ),
    );
  }
}

/// Home card for an event with genuine live coverage.
class _LiveNowCard extends StatelessWidget {
  const _LiveNowCard({
    required this.eventName,
    required this.competitionName,
    required this.editionLabel,
  });

  final String eventName;
  final String competitionName;
  final String editionLabel;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return SizedBox(
      width: 240,
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: () => GoRouter.of(context).go('/live'),
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.md),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Expanded(
                      child: Text(competitionName,
                          style: theme.textTheme.bodySmall,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis),
                    ),
                    const LiveBadge(),
                  ],
                ),
                const Spacer(),
                Text(eventName,
                    style: theme.textTheme.titleMedium,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis),
                const SizedBox(height: AppSpacing.xs),
                Text(editionLabel,
                    style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSurface
                            .withValues(alpha: 0.6))),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _EventCard extends StatelessWidget {
  const _EventCard({this.edition, required this.competition});

  final Edition? edition;
  final Competition competition;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final e = edition;
    return SizedBox(
      width: 240,
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: () {
            final sport = competition.sport;
            if (sport != null) context.go('/sports/${sport.code}');
          },
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.md),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(sportIcon(competition.sport?.icon),
                        size: 20, color: theme.colorScheme.primary),
                    const Spacer(),
                    // No LIVE badge here: these are editions, and an edition
                    // in progress is not live coverage.
                    if (e?.isInProgress ?? false)
                      Text('IN PROGRESS',
                          style: theme.textTheme.labelSmall?.copyWith(
                              color: AppColors.energy,
                              fontWeight: FontWeight.w800)),
                  ],
                ),
                const Spacer(),
                Text(
                  competition.name,
                  style: theme.textTheme.titleMedium,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
                const SizedBox(height: AppSpacing.xs),
                Text(
                  e == null
                      ? competition.level.toUpperCase()
                      : [
                          e.label,
                          if (e.hostCity != null) e.hostCity!,
                          if (e.hostCountry?.flagEmoji != null)
                            e.hostCountry!.flagEmoji!,
                        ].join(' · '),
                  style: theme.textTheme.bodySmall?.copyWith(
                      color: theme.colorScheme.onSurface.withValues(alpha: 0.6)),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _AthleteCard extends StatelessWidget {
  const _AthleteCard({required this.athlete});

  final Athlete athlete;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final initials = athlete.fullName
        .split(' ')
        .where((p) => p.isNotEmpty)
        .take(2)
        .map((p) => p[0])
        .join();
    return SizedBox(
      width: 128,
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              CircleAvatar(
                radius: 22,
                backgroundColor:
                    theme.colorScheme.primary.withValues(alpha: 0.15),
                child: Text(initials,
                    style: theme.textTheme.titleMedium
                        ?.copyWith(color: theme.colorScheme.primary)),
              ),
              const Spacer(),
              Flexible(
                child: Text(athlete.fullName,
                    style: theme.textTheme.titleSmall
                        ?.copyWith(fontWeight: FontWeight.w700),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis),
              ),
              const SizedBox(height: 2),
              Text(
                '${athlete.country?.flagEmoji ?? ''} '
                        '${athlete.country?.iso3 ?? ''}'
                    .trim(),
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

class _HomeSkeleton extends StatelessWidget {
  const _HomeSkeleton();

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(AppSpacing.md),
      children: const [
        SkeletonBox(height: 24, width: 140),
        SizedBox(height: AppSpacing.md),
        SkeletonBox(height: 120, radius: AppSpacing.cardRadius),
        SizedBox(height: AppSpacing.lg),
        SkeletonBox(height: 24, width: 180),
        SizedBox(height: AppSpacing.md),
        SkeletonBox(height: 148, radius: AppSpacing.cardRadius),
        SizedBox(height: AppSpacing.lg),
        SkeletonBox(height: 24, width: 160),
        SizedBox(height: AppSpacing.md),
        SkeletonBox(height: 132, radius: AppSpacing.cardRadius),
      ],
    );
  }
}
