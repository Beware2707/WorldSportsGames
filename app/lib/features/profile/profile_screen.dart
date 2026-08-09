import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/network/api_client.dart';
import '../../core/theme/app_theme.dart';
import '../../data/repositories.dart';
import '../../domain/models.dart';

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

class ProfileScreen extends ConsumerStatefulWidget {
  const ProfileScreen({super.key});

  @override
  ConsumerState<ProfileScreen> createState() => _ProfileScreenState();
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
          onPressed: () {
            ref.read(authRepositoryProvider).logout();
            ref.read(currentUserProvider.notifier).set(null);
          },
          icon: const Icon(Icons.logout_rounded),
          label: const Text('Sign out'),
        ),
      ],
    );
  }
}
