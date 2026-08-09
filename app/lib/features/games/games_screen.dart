import 'package:flutter/material.dart';

import '../../core/widgets/common.dart';

/// The modular mini-game engine (reaction, timing and accuracy challenges,
/// XP and leaderboards) is scheduled for Sprint 5.
class GamesScreen extends StatelessWidget {
  const GamesScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Games')),
      body: const EmptyState(
        icon: Icons.sports_esports_rounded,
        title: 'Sports games are coming',
        message: 'Reaction challenges, accuracy contests, XP and leaderboards '
            'arrive in a future release.',
      ),
    );
  }
}
