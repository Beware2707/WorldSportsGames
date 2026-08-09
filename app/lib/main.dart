import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'core/routing/app_router.dart';
import 'core/theme/app_theme.dart';
import 'features/profile/profile_screen.dart';

void main() {
  runApp(const ProviderScope(child: WorldSportsApp()));
}

class WorldSportsApp extends ConsumerWidget {
  const WorldSportsApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeMode = ref.watch(themeModeProvider);
    ref.watch(sessionRestoreProvider); // kick off persisted-session restore
    return MaterialApp.router(
      title: 'World Sports',
      theme: AppTheme.light(),
      darkTheme: AppTheme.dark(),
      themeMode: themeMode,
      routerConfig: appRouter,
      debugShowCheckedModeBanner: false,
    );
  }
}
