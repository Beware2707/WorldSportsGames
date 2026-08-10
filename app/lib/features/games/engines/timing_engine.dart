import 'dart:math';

import 'package:flutter/material.dart';

import '../../../core/theme/app_theme.dart';
import 'game_engine.dart';

/// Timing mechanic: stop the meter in the scoring band.
///
/// Closer to the centre scores more, so the score is a quality total rather
/// than a hit count (higher is better).
class TimingEngine extends GameEngineWidget {
  const TimingEngine({
    super.key,
    required super.game,
    required super.onFinished,
  });

  @override
  State<TimingEngine> createState() => _TimingEngineState();
}

class _TimingEngineState extends State<TimingEngine>
    with SingleTickerProviderStateMixin {
  late final AnimationController _meter = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 900),
  )..repeat(reverse: true);

  final _scores = <int>[];
  int _attempt = 0;
  int? _lastPoints;

  int get _attempts =>
      widget.game.configInt('serves', widget.game.configInt('balls', 5));

  @override
  void dispose() {
    _meter.dispose();
    super.dispose();
  }

  void _strike() {
    if (_attempt >= _attempts) return;
    // Centre of the sweep is perfect; falls off linearly to the edges.
    final distance = (_meter.value - 0.5).abs() * 2; // 0 = perfect, 1 = worst
    final points = max(0, (10 * (1 - distance)).round());
    _scores.add(points);
    _attempt++;

    setState(() => _lastPoints = points);

    if (_attempt >= _attempts) {
      _meter.stop();
      widget.onFinished(
        _scores.fold<int>(0, (a, b) => a + b).toDouble(),
        {'attempts': _scores},
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Column(
            children: [
              Text('Attempt ${min(_attempt + 1, _attempts)} of $_attempts',
                  style: theme.textTheme.labelLarge),
              const SizedBox(height: AppSpacing.xs),
              Text(
                _lastPoints == null
                    ? 'Strike when the bar is centred'
                    : '$_lastPoints points',
                style: theme.textTheme.bodyMedium,
              ),
            ],
          ),
        ),
        Expanded(
          child: Center(
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.lg),
              child: AnimatedBuilder(
                animation: _meter,
                builder: (context, _) => LayoutBuilder(
                  builder: (context, constraints) => SizedBox(
                    height: 96,
                    child: Stack(
                      alignment: Alignment.center,
                      children: [
                        Container(
                          height: 56,
                          decoration: BoxDecoration(
                            gradient: LinearGradient(colors: [
                              AppColors.live.withValues(alpha: 0.25),
                              AppColors.secondary.withValues(alpha: 0.55),
                              AppColors.live.withValues(alpha: 0.25),
                            ]),
                            borderRadius:
                                BorderRadius.circular(AppSpacing.chipRadius),
                          ),
                        ),
                        Positioned(
                          left: (_meter.value * constraints.maxWidth) - 3,
                          child: Container(
                            height: 76,
                            width: 6,
                            decoration: BoxDecoration(
                              color: theme.colorScheme.primary,
                              borderRadius: BorderRadius.circular(3),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.all(AppSpacing.lg),
          child: FilledButton.icon(
            onPressed: _attempt >= _attempts ? null : _strike,
            icon: const Icon(Icons.sports_tennis_rounded),
            label: const Text('Strike'),
            style: FilledButton.styleFrom(
              minimumSize: const Size.fromHeight(56),
            ),
          ),
        ),
      ],
    );
  }
}
