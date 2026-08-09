import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../../features/athletes/athletes_screen.dart';
import '../../features/competitions/competition_detail_screen.dart';
import '../../features/competitions/competitions_screen.dart';
import '../../features/competitions/edition_events_screen.dart';
import '../../features/events/event_detail_screen.dart';
import '../../features/games/games_screen.dart';
import '../../features/home/home_screen.dart';
import '../../features/live/live_screen.dart';
import '../../features/profile/profile_screen.dart';
import '../../features/sports/sport_detail_screen.dart';
import '../../features/sports/sports_screen.dart';

final appRouter = GoRouter(
  initialLocation: '/home',
  routes: [
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
                builder: (_, state) => EditionEventsScreen(
                    editionId:
                        int.parse(state.pathParameters['editionId']!)),
                routes: [
                  GoRoute(
                    path: 'events/:eventId',
                    builder: (_, state) => EventDetailScreen(
                        eventId:
                            int.parse(state.pathParameters['eventId']!)),
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
          GoRoute(path: '/games', builder: (_, _) => const GamesScreen()),
        ]),
        StatefulShellBranch(routes: [
          GoRoute(path: '/profile', builder: (_, _) => const ProfileScreen()),
        ]),
      ],
    ),
  ],
);

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
