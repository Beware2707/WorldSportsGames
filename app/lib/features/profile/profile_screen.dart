import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets/common.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';
import '../../domain/personalization_models.dart';
import '../follows/follows_controller.dart';

/// App-wide theme mode, toggled from the profile screen.
class ThemeModeNotifier extends Notifier<ThemeMode> {
  @override
  ThemeMode build() => ThemeMode.system;

  void set(ThemeMode mode) => state = mode;
}

final themeModeProvider =
    NotifierProvider<ThemeModeNotifier, ThemeMode>(ThemeModeNotifier.new);

/// Signed-in account, or null. Token is in-memory for Sprint 1; secure
/// persistence ships with the personalization sprint.
class CurrentUserNotifier extends Notifier<UserAccount?> {
  @override
  UserAccount? build() => null;

  void set(UserAccount? user) => state = user;
}

final currentUserProvider =
    NotifierProvider<CurrentUserNotifier, UserAccount?>(CurrentUserNotifier.new);

/// One-shot session restore from secure storage at app start.
final sessionRestoreProvider = FutureProvider<void>((ref) async {
  final user = await ref.read(authRepositoryProvider).restoreSession();
  if (user != null) ref.read(currentUserProvider.notifier).set(user);
});

class ProfileScreen extends ConsumerStatefulWidget {
  const ProfileScreen({super.key});

  @override
  ConsumerState<ProfileScreen> createState() => _ProfileScreenState();
}

/// Everything the signed-in user follows, grouped by kind, with unfollow.
class _FollowingCard extends ConsumerWidget {
  const _FollowingCard();

  static const _groupTitles = {
    FollowKind.sport: 'Sports',
    FollowKind.athlete: 'Athletes',
    FollowKind.country: 'Countries',
    FollowKind.competition: 'Competitions',
    FollowKind.discipline: 'Disciplines',
  };

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final follows = ref.watch(followsProvider);
    return Card(
      child: follows.when(
        loading: () => const Padding(
          padding: EdgeInsets.all(AppSpacing.lg),
          child: Center(child: CircularProgressIndicator()),
        ),
        error: (e, _) => Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: ErrorState(
            message: e.toString(),
            onRetry: () => ref.invalidate(followsProvider),
          ),
        ),
        data: (items) {
          if (items.isEmpty) {
            return ListTile(
              leading: const Icon(Icons.favorite_outline_rounded),
              title: const Text('Not following anything yet'),
              subtitle: const Text('Follow sports and athletes to shape '
                  'your home feed'),
              trailing: const Icon(Icons.chevron_right_rounded),
              onTap: () => context.push('/onboarding'),
            );
          }
          final grouped = <FollowKind, List<Follow>>{};
          for (final follow in items) {
            grouped.putIfAbsent(follow.kind, () => []).add(follow);
          }
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              ListTile(
                leading: const Icon(Icons.favorite_rounded),
                title: Text('Following ${items.length}'),
              ),
              for (final entry in grouped.entries) ...[
                Padding(
                  padding: const EdgeInsets.fromLTRB(
                      AppSpacing.md, AppSpacing.xs, AppSpacing.md, AppSpacing.xs),
                  child: Text(
                    _groupTitles[entry.key] ?? entry.key.name,
                    style: Theme.of(context).textTheme.labelLarge,
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(
                      horizontal: AppSpacing.md, vertical: AppSpacing.xs),
                  child: Wrap(
                    spacing: AppSpacing.sm,
                    runSpacing: AppSpacing.sm,
                    children: [
                      for (final follow in entry.value)
                        InputChip(
                          label: Text(follow.name ?? '#${follow.entityId}'),
                          // Awaited and caught: toggle() rethrows after
                          // rolling back, so a fire-and-forget call would
                          // surface as an uncaught async error with no
                          // feedback to the user.
                          onDeleted: () async {
                            final messenger = ScaffoldMessenger.of(context);
                            try {
                              await ref
                                  .read(followsProvider.notifier)
                                  .toggle(follow.kind, follow.entityId);
                            } on ApiException catch (e) {
                              messenger.showSnackBar(
                                  SnackBar(content: Text(e.message)));
                            }
                          },
                          deleteIcon: const Icon(Icons.close_rounded, size: 18),
                        ),
                    ],
                  ),
                ),
              ],
              const SizedBox(height: AppSpacing.sm),
            ],
          );
        },
      ),
    );
  }
}

class _ProfileScreenState extends ConsumerState<ProfileScreen> {
  final _email = TextEditingController();
  final _password = TextEditingController();
  final _displayName = TextEditingController();
  bool _registering = false;
  bool _busy = false;

  @override
  void dispose() {
    _email.dispose();
    _password.dispose();
    _displayName.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    setState(() => _busy = true);
    final auth = ref.read(authRepositoryProvider);
    final messenger = ScaffoldMessenger.of(context);
    try {
      if (_registering) {
        await auth.register(
          email: _email.text.trim(),
          password: _password.text,
          displayName: _displayName.text.trim(),
        );
      }
      final user = await auth.login(
        email: _email.text.trim(),
        password: _password.text,
      );
      ref.read(currentUserProvider.notifier).set(user);
    } on ApiException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final user = ref.watch(currentUserProvider);
    final mode = ref.watch(themeModeProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Profile')),
      body: ListView(
        padding: const EdgeInsets.all(AppSpacing.md),
        children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(AppSpacing.lg),
              child: user == null ? _authForm() : _accountView(user),
            ),
          ),
          if (user != null) ...[
            const SizedBox(height: AppSpacing.md),
            const _FollowingCard(),
            const SizedBox(height: AppSpacing.md),
            Card(
              child: Column(
                children: [
                  ListTile(
                    leading: const Icon(Icons.tune_rounded),
                    title: const Text('Personalize my feed'),
                    subtitle: const Text('Pick sports, athletes and more'),
                    trailing: const Icon(Icons.chevron_right_rounded),
                    onTap: () => context.push('/onboarding'),
                  ),
                  ListTile(
                    leading: const Icon(Icons.notifications_outlined),
                    title: const Text('Notifications'),
                    subtitle: const Text('Choose what you hear about'),
                    trailing: const Icon(Icons.chevron_right_rounded),
                    onTap: () => context.push('/settings/notifications'),
                  ),
                ],
              ),
            ),
          ],
          const SizedBox(height: AppSpacing.md),
          Card(
            child: Column(
              children: [
                const ListTile(
                  leading: Icon(Icons.palette_outlined),
                  title: Text('Appearance'),
                ),
                RadioGroup<ThemeMode>(
                  groupValue: mode,
                  onChanged: (v) => ref
                      .read(themeModeProvider.notifier)
                      .set(v ?? ThemeMode.system),
                  child: const Column(
                    children: [
                      RadioListTile(
                          value: ThemeMode.system, title: Text('System')),
                      RadioListTile(value: ThemeMode.light, title: Text('Light')),
                      RadioListTile(value: ThemeMode.dark, title: Text('Dark')),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _authForm() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(_registering ? 'Create account' : 'Sign in',
            style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: AppSpacing.md),
        if (_registering) ...[
          TextField(
            controller: _displayName,
            decoration: const InputDecoration(labelText: 'Display name'),
            textInputAction: TextInputAction.next,
          ),
          const SizedBox(height: AppSpacing.sm),
        ],
        TextField(
          controller: _email,
          decoration: const InputDecoration(labelText: 'Email'),
          keyboardType: TextInputType.emailAddress,
          textInputAction: TextInputAction.next,
        ),
        const SizedBox(height: AppSpacing.sm),
        TextField(
          controller: _password,
          decoration: const InputDecoration(
              labelText: 'Password', helperText: 'At least 8 characters'),
          obscureText: true,
          onSubmitted: (_) => _busy ? null : _submit(),
        ),
        const SizedBox(height: AppSpacing.md),
        FilledButton(
          onPressed: _busy ? null : _submit,
          child: _busy
              ? const SizedBox(
                  height: 18, width: 18, child: CircularProgressIndicator())
              : Text(_registering ? 'Register & sign in' : 'Sign in'),
        ),
        TextButton(
          onPressed: () => setState(() => _registering = !_registering),
          child: Text(_registering
              ? 'Have an account? Sign in'
              : 'New here? Create an account'),
        ),
      ],
    );
  }

  Widget _accountView(UserAccount user) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Hi, ${user.displayName}',
            style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: AppSpacing.xs),
        Text(user.email),
        const SizedBox(height: AppSpacing.md),
        OutlinedButton.icon(
          onPressed: () async {
            await ref.read(authRepositoryProvider).logout();
            ref.read(currentUserProvider.notifier).set(null);
          },
          icon: const Icon(Icons.logout_rounded),
          label: const Text('Sign out'),
        ),
      ],
    );
  }
}
