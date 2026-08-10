import 'dart:math';

import 'package:flutter/material.dart';

import '../../../core/theme/app_theme.dart';
import 'game_engine.dart';

/// Accuracy mechanic: hit the moving target zone.
///
/// A marker sweeps a bar; tapping inside the target scores. Score is the
/// number of successful attempts (higher is better).
class AccuracyEngine extends GameEngineWidget {
  const AccuracyEngine({
    super.key,
    required super.game,
    required super.onFinished,
  });

  @override
  State<AccuracyEngine> createState() => _AccuracyEngineState();
}

class _AccuracyEngineState extends State<AccuracyEngine>
    with SingleTickerProviderStateMixin {
  late final AnimationController _sweep = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1100),
  )..repeat(reverse: true);

  final _random = Random();
  final _outcomes = <bool>[];
  late double _targetCentre = _random.nextDouble() * 0.6 + 0.2;
  int _attempt = 0;
  bool? _lastHit;

  int get _attempts => widget.game.configInt('attempts', 5);
  // Target narrows as you progress, so a perfect run takes real precision.
  double get _halfWidth => 0.12 - (_attempt * 0.008).clamp(0.0, 0.06);

  @override
  void dispose() {
    _sweep.dispose();
    super.dispose();
  }

  void _shoot() {
    if (_attempt >= _attempts) return;
    final marker = _sweep.value;
    final hit = (marker - _targetCentre).abs() <= _halfWidth;
    _outcomes.add(hit);
    _attempt++;

    setState(() {
      _lastHit = hit;
      _targetCentre = _random.nextDouble() * 0.6 + 0.2;
    });

    if (_attempt >= _attempts) {
      _sweep.stop();
      final score = _outcomes.where((x) => x).length.toDouble();
      widget.onFinished(score, {'attempts': _outcomes});
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
                _lastHit == null
                    ? 'Tap when the marker is inside the target'
                    : (_lastHit! ? 'Hit!' : 'Missed'),
                style: theme.textTheme.bodyMedium?.copyWith(
                  color: _lastHit == null
                      ? null
                      : (_lastHit! ? AppColors.secondary : AppColors.live),
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: Center(
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: AppSpacing.lg),
              child: AnimatedBuilder(
                animation: _sweep,
                builder: (context, _) => LayoutBuilder(
                  builder: (context, constraints) {
                    final width = constraints.maxWidth;
                    return SizedBox(
                      height: 96,
                      child: Stack(
                        alignment: Alignment.centerLeft,
                        children: [
                          Container(
                            height: 56,
                            decoration: BoxDecoration(
                              color: theme.colorScheme.surfaceContainerHighest,
                              borderRadius:
                                  BorderRadius.circular(AppSpacing.chipRadius),
                            ),
                          ),
                          Positioned(
                            left: (_targetCentre - _halfWidth) * width,
                            child: Container(
                              height: 56,
                              width: _halfWidth * 2 * width,
                              decoration: BoxDecoration(
                                color: AppColors.secondary
                                    .withValues(alpha: 0.35),
                                borderRadius: BorderRadius.circular(
                                    AppSpacing.chipRadius),
                              ),
                            ),
                          ),
                          Positioned(
                            left: (_sweep.value * width) - 3,
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
                    );
                  },
                ),
              ),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.all(AppSpacing.lg),
          child: FilledButton.icon(
            onPressed: _attempt >= _attempts ? null : _shoot,
            icon: const Icon(Icons.my_location_rounded),
            label: const Text('Shoot'),
            style: FilledButton.styleFrom(
              minimumSize: const Size.fromHeight(56),
            ),
          ),
        ),
      ],
    );
  }
}
