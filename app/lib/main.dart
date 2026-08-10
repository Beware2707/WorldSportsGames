import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'core/i18n/app_strings.dart';
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
      supportedLocales: AppStrings.supportedLocales,
      // Untranslated locales fall back to English rather than failing.
      localeResolutionCallback: (locale, supported) =>
          supported.firstWhere(
            (l) => l.languageCode == locale?.languageCode,
            orElse: () => supported.first,
          ),
      builder: (context, child) => AppStringsScope(
        strings: AppStrings.lookup(Localizations.maybeLocaleOf(context)),
        child: child ?? const SizedBox.shrink(),
      ),
    );
  }
}
