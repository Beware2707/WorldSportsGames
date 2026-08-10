import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../../features/athletes/athlete_profile_screen.dart';
import '../../features/athletes/athletes_screen.dart';
import '../../features/countries/country_profile_screen.dart';
import '../../features/countries/medal_table_screen.dart';
import '../../features/competitions/competition_detail_screen.dart';
import '../../features/competitions/competitions_screen.dart';
import '../../features/competitions/edition_events_screen.dart';
import '../../features/events/event_detail_screen.dart';
import '../../features/games/game_play_screen.dart';
import '../../features/games/games_screen.dart';
import '../../features/games/leaderboard_screen.dart';
import '../../features/home/home_screen.dart';
import '../../features/live/live_screen.dart';
import '../../features/onboarding/onboarding_screen.dart';
import '../../features/rankings/rankings_screen.dart';
import '../../features/records/records_screen.dart';
import '../../features/profile/profile_screen.dart';
import '../../features/search/search_screen.dart';
import '../../features/settings/notification_settings_screen.dart';
import '../../features/sports/sport_detail_screen.dart';
import '../../features/sports/sports_screen.dart';
import '../widgets/common.dart';

final appRouter = GoRouter(
  initialLocation: '/home',
  routes: [
    // Full-screen routes outside the bottom-nav shell.
    GoRoute(path: '/onboarding', builder: (_, _) => const OnboardingScreen()),
    GoRoute(path: '/search', builder: (_, _) => const SearchScreen()),
    GoRoute(
      path: '/athletes/:slug',
      builder: (_, state) =>
          AthleteProfileScreen(slug: state.pathParameters['slug']!),
    ),
    GoRoute(
      path: '/countries/:iso3',
      builder: (_, state) =>
          CountryProfileScreen(iso3: state.pathParameters['iso3']!),
    ),
    GoRoute(
      path: '/events/:eventId',
      builder: (_, state) => _byIntParam(
        state.pathParameters['eventId'],
        (id) => EventDetailScreen(eventId: id),
      ),
    ),
    GoRoute(path: '/rankings', builder: (_, _) => const RankingsScreen()),
    GoRoute(path: '/records', builder: (_, _) => const RecordsScreen()),
    GoRoute(path: '/medals', builder: (_, _) => const MedalTableScreen()),
    GoRoute(
      path: '/settings/notifications',
      builder: (_, _) => const NotificationSettingsScreen(),
    ),
    StatefulShellRoute.indexedStack(
      builder: (context, state, shell) => _AppShell(shell: shell),
      branches: [
        StatefulShellBranch(routes: [
          GoRoute(
            path: '/home',
            builder: (_, _) => const HomeScreen(),
            routes: [
              GoRoute(
                  path: 'athletes',
                  builder: (_, _) => const AthletesScreen()),
              GoRoute(
                path: 'competitions',
                builder: (_, _) => const CompetitionsScreen(),
              ),
            ],
          ),
          // Deep-linkable aliases outside the /home prefix.
          GoRoute(
            path: '/competitions/:slug',
            builder: (_, state) => CompetitionDetailScreen(
                slug: state.pathParameters['slug']!),
            routes: [
              GoRoute(
                path: 'editions/:editionId',
                // Deep links are untrusted input: a non-numeric segment must
                // render a not-found screen, not throw FormatException out of
                // the route builder.
                builder: (_, state) => _byIntParam(
                  state.pathParameters['editionId'],
                  (id) => EditionEventsScreen(editionId: id),
                ),
                routes: [
                  GoRoute(
                    path: 'events/:eventId',
                    builder: (_, state) => _byIntParam(
                      state.pathParameters['eventId'],
                      (id) => EventDetailScreen(eventId: id),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ]),
        StatefulShellBranch(routes: [
          GoRoute(path: '/live', builder: (_, _) => const LiveScreen()),
        ]),
        StatefulShellBranch(routes: [
          GoRoute(
            path: '/sports',
            builder: (_, _) => const SportsScreen(),
            routes: [
              GoRoute(
                path: ':code',
                builder: (_, state) =>
                    SportDetailScreen(code: state.pathParameters['code']!),
              ),
            ],
          ),
        ]),
        StatefulShellBranch(routes: [
          GoRoute(
            path: '/games',
            builder: (_, _) => const GamesScreen(),
            routes: [
              GoRoute(
                path: ':code',
                builder: (_, state) =>
                    GamePlayScreen(code: state.pathParameters['code']!),
                routes: [
                  GoRoute(
                    path: 'leaderboard',
                    builder: (_, state) =>
                        LeaderboardScreen(code: state.pathParameters['code']!),
                  ),
                ],
              ),
            ],
          ),
        ]),
        StatefulShellBranch(routes: [
          GoRoute(path: '/profile', builder: (_, _) => const ProfileScreen()),
        ]),
      ],
    ),
  ],
);

Widget _byIntParam(String? raw, Widget Function(int) build) {
  final id = int.tryParse(raw ?? '');
  if (id == null) {
    return Scaffold(
      appBar: AppBar(),
      body: const EmptyState(
        icon: Icons.link_off_rounded,
        title: 'Page not found',
        message: 'That link points to something we cannot open.',
      ),
    );
  }
  return build(id);
}

class _AppShell extends StatelessWidget {
  const _AppShell({required this.shell});

  final StatefulNavigationShell shell;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: shell,
      bottomNavigationBar: NavigationBar(
        selectedIndex: shell.currentIndex,
        onDestinationSelected: (i) =>
            shell.goBranch(i, initialLocation: i == shell.currentIndex),
        destinations: const [
          NavigationDestination(
              icon: Icon(Icons.home_outlined),
              selectedIcon: Icon(Icons.home_rounded),
              label: 'Home'),
          NavigationDestination(
              icon: Icon(Icons.bolt_outlined),
              selectedIcon: Icon(Icons.bolt_rounded),
              label: 'Live'),
          NavigationDestination(
              icon: Icon(Icons.emoji_events_outlined),
              selectedIcon: Icon(Icons.emoji_events_rounded),
              label: 'Sports'),
          NavigationDestination(
              icon: Icon(Icons.sports_esports_outlined),
              selectedIcon: Icon(Icons.sports_esports_rounded),
              label: 'Games'),
          NavigationDestination(
              icon: Icon(Icons.person_outline_rounded),
              selectedIcon: Icon(Icons.person_rounded),
              label: 'Profile'),
        ],
      ),
    );
  }
}
