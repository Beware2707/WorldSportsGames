import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/competitive_repository.dart';
import '../../domain/competitive_models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follow_button.dart';

final countryProfileProvider =
    FutureProvider.autoDispose.family<CountryProfile, String>(
  (ref, iso3) => ref.watch(competitiveRepositoryProvider).countryProfile(iso3),
  retry: (count, error) => null,
);

class CountryProfileScreen extends ConsumerWidget {
  const CountryProfileScreen({super.key, required this.iso3});

  final String iso3;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profile = ref.watch(countryProfileProvider(iso3));
    return Scaffold(
      appBar: AppBar(title: Text(profile.value?.country.name ?? '')),
      body: profile.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => ErrorState(
          message: e.toString(),
          onRetry: () => ref.invalidate(countryProfileProvider(iso3)),
        ),
        data: (data) => ListView(
          padding: const EdgeInsets.all(AppSpacing.md),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(AppSpacing.lg),
                child: Row(
                  children: [
                    Text(data.country.flagEmoji ?? '',
                        style: const TextStyle(fontSize: 40)),
                    const SizedBox(width: AppSpacing.md),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(data.country.name,
                              style: Theme.of(context).textTheme.titleLarge),
                          Text('${data.country.iso3} · '
                              '${data.athleteCount} athletes'),
                        ],
                      ),
                    ),
                    FollowButton(
                      kind: FollowKind.country,
                      entityId: data.country.id,
                      name: data.country.name,
                    ),
                  ],
                ),
              ),
            ),
            if (data.medals != null) ...[
              const SectionHeader('Medals'),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(AppSpacing.lg),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                    children: [
                      _MedalStat(
                          label: 'Gold',
                          value: data.medals!.gold,
                          color: AppColors.medalGold),
                      _MedalStat(
                          label: 'Silver',
                          value: data.medals!.silver,
                          color: AppColors.medalSilver),
                      _MedalStat(
                          label: 'Bronze',
                          value: data.medals!.bronze,
                          color: AppColors.medalBronze),
                      _MedalStat(
                          label: 'Total',
                          value: data.medals!.total,
                          color: Theme.of(context).colorScheme.primary),
                    ],
                  ),
                ),
              ),
            ],
            if (data.records.isNotEmpty) ...[
              const SectionHeader('Records'),
              for (final record in data.records)
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
                      subtitle: Text(record.holderName ?? 'Unknown holder'),
                      trailing: Text(record.valueText,
                          style: Theme.of(context)
                              .textTheme
                              .titleMedium
                              ?.copyWith(fontWeight: FontWeight.w800)),
                    ),
                  ),
                ),
            ],
            const SectionHeader('Athletes'),
            if (data.athletes.isEmpty)
              const EmptyState(
                icon: Icons.person_search_rounded,
                title: 'No athletes listed yet',
              ),
            for (final athlete in data.athletes)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpacing.sm),
                child: Card(
                  clipBehavior: Clip.antiAlias,
                  child: ListTile(
                    leading: CircleAvatar(
                      child: Text(athlete.fullName.isEmpty
                          ? '?'
                          : athlete.fullName[0]),
                    ),
                    title: Text(athlete.fullName),
                    trailing: const Icon(Icons.chevron_right_rounded),
                    onTap: () => context.push('/athletes/${athlete.slug}'),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _MedalStat extends StatelessWidget {
  const _MedalStat({
    required this.label,
    required this.value,
    required this.color,
  });

  final String label;
  final int value;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text('$value',
            style: Theme.of(context)
                .textTheme
                .headlineMedium
                ?.copyWith(color: color, fontWeight: FontWeight.w800)),
        Text(label, style: Theme.of(context).textTheme.labelSmall),
      ],
    );
  }
}
