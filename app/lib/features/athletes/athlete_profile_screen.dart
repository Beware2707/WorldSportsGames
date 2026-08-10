import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/competitive_repository.dart';
import '../../domain/competitive_models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follow_button.dart';

final athleteProfileProvider =
    FutureProvider.autoDispose.family<AthleteProfile, String>(
  (ref, slug) => ref.watch(competitiveRepositoryProvider).athleteProfile(slug),
  retry: (count, error) => null,
);

const _metalColors = {
  'gold': AppColors.medalGold,
  'silver': AppColors.medalSilver,
  'bronze': AppColors.medalBronze,
};

class AthleteProfileScreen extends ConsumerWidget {
  const AthleteProfileScreen({super.key, required this.slug});

  final String slug;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profile = ref.watch(athleteProfileProvider(slug));
    return Scaffold(
      appBar: AppBar(title: Text(profile.value?.athlete.fullName ?? '')),
      body: profile.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(athleteProfileProvider(slug)),
        ),
        data: (data) => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: [
            _Header(profile: data),
            if (data.medals.isNotEmpty) ...[
              const SectionHeader('Medals'),
              _MedalSummary(medals: data.medals),
              for (final medal in data.medals)
                Padding(
                  padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                  child: Card(
                    child: ListTile(
                      leading: Icon(Icons.workspace_premium_rounded,
                          color: _metalColors[medal.metal]),
                      title: Text(medal.eventName),
                      subtitle: Text([
                        if (medal.competitionName != null)
                          medal.competitionName!,
                        if (medal.editionLabel != null) medal.editionLabel!,
                      ].join(' · ')),
                    ),
                  ),
                ),
            ],
            if (data.rankings.isNotEmpty) ...[
              const SectionHeader('Rankings'),
              for (final ranking in data.rankings)
                Padding(
                  padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                  child: Card(
                    child: ListTile(
                      leading: CircleAvatar(child: Text('${ranking.rank}')),
                      title: Text(ranking.discipline ?? 'Overall'),
                      subtitle: Text(ranking.methodology.replaceAll('_', ' ')),
                      trailing: ranking.points == null
                          ? null
                          : Text('${ranking.points!.toStringAsFixed(0)} pts'),
                    ),
                  ),
                ),
            ],
            if (data.personalBests.isNotEmpty) ...[
              const SectionHeader('Bests & records'),
              for (final record in data.personalBests)
                Padding(
                  padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                  child: Card(
                    child: ListTile(
                      leading: Chip(
                        label: Text(record.kind),
                        visualDensity: VisualDensity.compact,
                        padding: EdgeInsets.zero,
                      ),
                      title: Text(record.eventName),
                      subtitle: Text(record.kindLabel),
                      trailing: Text(record.valueText,
                          style: Theme.of(context)
                              .textTheme
                              .titleMedium
                              ?.copyWith(fontWeight: FontWeight.w800)),
                    ),
                  ),
                ),
            ],
            const SectionHeader('Recent results'),
            if (data.recentResults.isEmpty)
              const EmptyState(
                icon: Icons.timeline_rounded,
                title: 'No results recorded yet',
              ),
            for (final result in data.recentResults)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                child: Card(
                  clipBehavior: Clip.antiAlias,
                  child: ListTile(
                    leading: CircleAvatar(
                      radius: 16,
                      backgroundColor: Theme.of(context)
                          .colorScheme
                          .onSurface
                          .withValues(alpha: 0.08),
                      child: Text(result.position?.toString() ?? '–',
                          style: Theme.of(context).textTheme.labelLarge),
                    ),
                    title: Text(result.eventName),
                    subtitle: Text(result.competitionName ?? ''),
                    trailing: Text(
                      result.resultStatus != null &&
                              result.resultStatus != 'ok'
                          ? result.resultStatus!
                          : (result.valueText ?? '—'),
                      style: Theme.of(context).textTheme.titleSmall?.copyWith(
                            fontWeight: FontWeight.w700,
                            fontFeatures: const [FontFeature.tabularFigures()],
                          ),
                    ),
                    onTap: () => context.push('/events/${result.eventId}'),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.profile});

  final AthleteProfile profile;

  @override
  Widget build(BuildContext context) {
    final athlete = profile.athlete;
    final initials = athlete.fullName
        .split(' ')
        .where((p) => p.isNotEmpty)
        .take(2)
        .map((p) => p[0])
        .join();
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AppSpacing.lg),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                CircleAvatar(radius: 28, child: Text(initials)),
                const SizedBox(width: AppSpacing.md),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(athlete.fullName,
                          style: Theme.of(context).textTheme.titleLarge),
                      const SizedBox(height: AppSpacing.xs),
                      Text([
                        if (athlete.country?.flagEmoji != null)
                          athlete.country!.flagEmoji!,
                        if (athlete.country != null) athlete.country!.name,
                      ].join(' ')),
                    ],
                  ),
                ),
                FollowButton(
                  kind: FollowKind.athlete,
                  entityId: athlete.id,
                  name: athlete.fullName,
                  compact: true,
                ),
              ],
            ),
            if (profile.disciplines.isNotEmpty) ...[
              const SizedBox(height: AppSpacing.md),
              Wrap(
                spacing: AppSpacing.sm,
                runSpacing: AppSpacing.xs,
                children: [
                  for (final discipline in profile.disciplines)
                    Chip(
                      label: Text(discipline.name),
                      visualDensity: VisualDensity.compact,
                    ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _MedalSummary extends StatelessWidget {
  const _MedalSummary({required this.medals});

  final List<AthleteMedal> medals;

  @override
  Widget build(BuildContext context) {
    int count(String metal) => medals.where((m) => m.metal == metal).length;
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpacing.sm),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: [
              for (final metal in ['gold', 'silver', 'bronze'])
                Column(
                  children: [
                    Text('${count(metal)}',
                        style: Theme.of(context)
                            .textTheme
                            .headlineSmall
                            ?.copyWith(
                                color: _metalColors[metal],
                                fontWeight: FontWeight.w800)),
                    Text(metal[0].toUpperCase() + metal.substring(1),
                        style: Theme.of(context).textTheme.labelSmall),
                  ],
                ),
            ],
          ),
        ),
      ),
    );
  }
}
