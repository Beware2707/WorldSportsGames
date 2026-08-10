import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

import '../../../core/theme/app_theme.dart';
import 'game_engine.dart';

/// Reaction mechanic: wait for the signal, tap as fast as you can.
///
/// Score is mean reaction time in milliseconds (lower is better). Tapping
/// before the signal is a false start and costs the round rather than
/// producing an impossibly fast time.
class ReactionEngine extends GameEngineWidget {
  const ReactionEngine({
    super.key,
    required super.game,
    required super.onFinished,
  });

  @override
  State<ReactionEngine> createState() => _ReactionEngineState();
}

enum _Phase { idle, waiting, go, falseStart, done }

class _ReactionEngineState extends State<ReactionEngine> {
  final _random = Random();
  final _times = <int>[];
  final _stopwatch = Stopwatch();
  Timer? _timer;
  _Phase _phase = _Phase.idle;
  int _round = 0;
  int? _lastMs;

  int get _rounds => widget.game.configInt('rounds', 5);

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  void _startRound() {
    setState(() {
      _phase = _Phase.waiting;
      _lastMs = null;
    });
    // Randomised so the delay can't be learned and pre-empted.
    _timer = Timer(Duration(milliseconds: 1200 + _random.nextInt(2600)), () {
      if (!mounted) return;
      _stopwatch
        ..reset()
        ..start();
      setState(() => _phase = _Phase.go);
    });
  }

  void _onTap() {
    switch (_phase) {
      case _Phase.idle:
      case _Phase.falseStart:
        _startRound();
      case _Phase.waiting:
        _timer?.cancel();
        setState(() => _phase = _Phase.falseStart);
      case _Phase.go:
        _stopwatch.stop();
        final ms = _stopwatch.elapsedMilliseconds;
        _times.add(ms);
        _round++;
        setState(() {
          _lastMs = ms;
          _phase = _round >= _rounds ? _Phase.done : _Phase.idle;
        });
        if (_phase == _Phase.done) _finish();
      case _Phase.done:
        break;
    }
  }

  void _finish() {
    final mean = _times.reduce((a, b) => a + b) / _times.length;
    widget.onFinished(mean, {'rounds': _times});
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final (color, title, subtitle) = switch (_phase) {
      _Phase.idle => (
          theme.colorScheme.surfaceContainerHighest,
          _round == 0 ? 'Tap to start' : 'Tap for round ${_round + 1}',
          _lastMs == null ? 'Wait for green, then tap' : '$_lastMs ms',
        ),
      _Phase.waiting => (
          AppColors.energy,
          'Wait…',
          'Tap when it turns green',
        ),
      _Phase.go => (AppColors.secondary, 'TAP!', ''),
      _Phase.falseStart => (
          AppColors.live,
          'False start',
          'Tap to try that round again',
        ),
      _Phase.done => (
          theme.colorScheme.surfaceContainerHighest,
          'Finished',
          'Submitting…',
        ),
    };

    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Text('Round ${min(_round + 1, _rounds)} of $_rounds',
              style: theme.textTheme.labelLarge),
        ),
        Expanded(
          child: Semantics(
            button: true,
            label: title,
            child: GestureDetector(
              onTap: _onTap,
              child: Container(
                width: double.infinity,
                margin: const EdgeInsets.all(AppSpacing.md),
                decoration: BoxDecoration(
                  color: color,
                  borderRadius: BorderRadius.circular(AppSpacing.cardRadius),
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(title,
                        style: theme.textTheme.headlineMedium
                            ?.copyWith(color: Colors.black87)),
                    if (subtitle.isNotEmpty) ...[
                      const SizedBox(height: AppSpacing.sm),
                      Text(subtitle,
                          style: theme.textTheme.bodyLarge
                              ?.copyWith(color: Colors.black54)),
                    ],
                  ],
                ),
              ),
            ),
          ),
        ),
        if (_times.isNotEmpty)
          Padding(
            padding: const EdgeInsets.all(AppSpacing.md),
            child: Text('Times: ${_times.join(" · ")} ms',
                style: theme.textTheme.bodySmall),
          ),
      ],
    );
  }
}
