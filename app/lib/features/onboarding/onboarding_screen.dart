import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/personalization_repository.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follows_controller.dart';

final _sportsProvider = FutureProvider.autoDispose<List<Sport>>(
  (ref) => ref.watch(catalogRepositoryProvider).listSports(),
  retry: (count, error) => null,
);

final _countriesProvider = FutureProvider.autoDispose<List<Country>>(
  (ref) => ref.watch(catalogRepositoryProvider).listCountries(),
  retry: (count, error) => null,
);

final _athletesProvider = FutureProvider.autoDispose<List<Athlete>>(
  (ref) async =>
      (await ref.watch(catalogRepositoryProvider).listAthletes()).items,
  retry: (count, error) => null,
);

final _competitionsProvider = FutureProvider.autoDispose<List<Competition>>(
  (ref) async =>
      (await ref.watch(catalogRepositoryProvider).listCompetitions()).items,
  retry: (count, error) => null,
);

/// Four-step picker that seeds the personalized feed.
///
/// Every step is skippable — following nothing is a valid choice and still
/// yields a working (generic) home feed.
class OnboardingScreen extends ConsumerStatefulWidget {
  const OnboardingScreen({super.key});

  @override
  ConsumerState<OnboardingScreen> createState() => _OnboardingScreenState();
}

class _OnboardingScreenState extends ConsumerState<OnboardingScreen> {
  final _selected = <(FollowKind, int), String>{};
  final _controller = PageController();
  int _step = 0;
  bool _saving = false;

  static const _steps = [
    (FollowKind.sport, 'Pick your sports', 'Follow the sports you care about.'),
    (FollowKind.athlete, 'Follow athletes', 'Their results lead your feed.'),
    (FollowKind.country, 'Choose countries', 'Track national teams.'),
    (
      FollowKind.competition,
      'Add competitions',
      'Never miss a Games or a championship.'
    ),
  ];

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _toggle(FollowKind kind, int id, String name) => setState(() {
        final key = (kind, id);
        if (_selected.containsKey(key)) {
          _selected.remove(key);
        } else {
          _selected[key] = name;
        }
      });

  Future<void> _finish() async {
    setState(() => _saving = true);
    final messenger = ScaffoldMessenger.of(context);
    final router = GoRouter.of(context);
    try {
      await ref.read(personalizationRepositoryProvider).setFollows([
        for (final entry in _selected.entries)
          Follow(kind: entry.key.$1, entityId: entry.key.$2, name: entry.value),
      ]);
      ref.invalidate(followsProvider);
      router.go('/home');
    } on ApiException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final (kind, title, subtitle) = _steps[_step];
    final isLast = _step == _steps.length - 1;
    return Scaffold(
      appBar: AppBar(
        title: Text('Step ${_step + 1} of ${_steps.length}'),
        actions: [
          TextButton(
            onPressed: _saving ? null : _finish,
            child: Text(_selected.isEmpty ? 'Skip' : 'Done'),
          ),
        ],
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(4),
          child: LinearProgressIndicator(
            value: (_step + 1) / _steps.length,
          ),
        ),
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(
                AppSpacing.md, AppSpacing.md, AppSpacing.md, AppSpacing.sm),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: Theme.of(context).textTheme.headlineMedium),
                const SizedBox(height: AppSpacing.xs),
                Text(subtitle, style: Theme.of(context).textTheme.bodyMedium),
              ],
            ),
          ),
          Expanded(child: _picker(kind)),
          SafeArea(
            child: Padding(
              padding: const EdgeInsets.all(AppSpacing.md),
              child: Row(
                children: [
                  if (_step > 0)
                    TextButton(
                      onPressed: () => setState(() => _step--),
                      child: const Text('Back'),
                    ),
                  const Spacer(),
                  Text('${_selected.length} selected'),
                  const SizedBox(width: AppSpacing.md),
                  FilledButton(
                    onPressed: _saving
                        ? null
                        : isLast
                            ? _finish
                            : () => setState(() => _step++),
                    child: _saving
                        ? const SizedBox(
                            height: 18,
                            width: 18,
                            child: CircularProgressIndicator(strokeWidth: 2))
                        : Text(isLast ? 'Finish' : 'Next'),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _picker(FollowKind kind) {
    return switch (kind) {
      FollowKind.sport => _chips(
          ref.watch(_sportsProvider),
          (s) => (s.id, s.name),
          FollowKind.sport,
        ),
      FollowKind.athlete => _chips(
          ref.watch(_athletesProvider),
          (a) => (a.id, a.fullName),
          FollowKind.athlete,
        ),
      FollowKind.country => _chips(
          ref.watch(_countriesProvider),
          (c) => (c.id, '${c.flagEmoji ?? ''} ${c.name}'.trim()),
          FollowKind.country,
        ),
      FollowKind.competition => _chips(
          ref.watch(_competitionsProvider),
          (c) => (c.id, c.name),
          FollowKind.competition,
        ),
      FollowKind.discipline => const SizedBox.shrink(),
    };
  }

  Widget _chips<T>(
    AsyncValue<List<T>> async,
    (int, String) Function(T) extract,
    FollowKind kind,
  ) {
    return async.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (e, _) => ErrorState(
        message: e.toString(),
        onRetry: () {
          ref.invalidate(_sportsProvider);
          ref.invalidate(_athletesProvider);
          ref.invalidate(_countriesProvider);
          ref.invalidate(_competitionsProvider);
        },
      ),
      data: (items) => items.isEmpty
          ? const EmptyState(
              icon: Icons.search_off_rounded, title: 'Nothing to choose yet')
          : SingleChildScrollView(
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.md),
              child: Wrap(
                spacing: AppSpacing.sm,
                runSpacing: AppSpacing.sm,
                children: [
                  for (final item in items)
                    Builder(builder: (context) {
                      final (id, label) = extract(item);
                      final selected = _selected.containsKey((kind, id));
                      return FilterChip(
                        label: Text(label),
                        selected: selected,
                        onSelected: (_) => _toggle(kind, id, label),
                      );
                    }),
                ],
              ),
            ),
    );
  }
}
