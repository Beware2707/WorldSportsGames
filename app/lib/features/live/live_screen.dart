import 'package:flutter/material.dart';

import '../../core/widgets/common.dart';

/// Sprint 2 brings the real Live Center (WebSocket diff stream). Until then
/// this screen is an honest placeholder — it never fabricates live data.
class LiveScreen extends StatelessWidget {
  const LiveScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Live')),
      body: const EmptyState(
        icon: Icons.sensors_rounded,
        title: 'Live Center is on its way',
        message: 'Real-time scores, results and event timelines land in the '
            'next release. Nothing shown here will ever be simulated.',
      ),
    );
  }
}
