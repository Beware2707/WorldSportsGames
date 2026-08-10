import 'dart:math';

import 'package:flutter/material.dart';

import '../../../core/theme/app_theme.dart';
import 'game_engine.dart';

/// Sequence mechanic: clear the gates in the order shown.
///
/// The pattern is revealed once, then must be repeated. Score is the number
/// of gates cleared before the first mistake (higher is better).
class SequenceEngine extends GameEngineWidget {
  const SequenceEngine({
    super.key,
    required super.game,
    required super.onFinished,
  });

  @override
  State<SequenceEngine> createState() => _SequenceEngineState();
}

class _SequenceEngineState extends State<SequenceEngine> {
  static const _tiles = 6;
  final _random = Random();
  late final List<int> _pattern = List.generate(
    widget.game.configInt('gates', 8),
    (_) => _random.nextInt(_tiles),
  );

  int _cursor = 0;
  int _revealIndex = 0;
  bool _revealing = true;
  int? _highlight;

  @override
  void initState() {
    super.initState();
    _revealNext();
  }

  Future<void> _revealNext() async {
    // Reveal the whole pattern once before accepting input.
    while (_revealIndex < _pattern.length) {
      if (!mounted) return;
      setState(() => _highlight = _pattern[_revealIndex]);
      await Future<void>.delayed(const Duration(milliseconds: 420));
      if (!mounted) return;
      setState(() => _highlight = null);
      await Future<void>.delayed(const Duration(milliseconds: 140));
      _revealIndex++;
    }
    if (!mounted) return;
    setState(() => _revealing = false);
  }

  void _tap(int tile) {
    if (_revealing) return;
    if (tile == _pattern[_cursor]) {
      setState(() => _cursor++);
      if (_cursor >= _pattern.length) {
        widget.onFinished(_cursor.toDouble(), {'gates': _pattern.length});
      }
    } else {
      // First mistake ends the run — the score is what you cleared.
      widget.onFinished(_cursor.toDouble(), {'gates': _pattern.length});
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
              Text(
                _revealing
                    ? 'Watch the gate order'
                    : 'Gate ${_cursor + 1} of ${_pattern.length}',
                style: theme.textTheme.labelLarge,
              ),
              const SizedBox(height: AppSpacing.xs),
              Text(
                _revealing ? 'Memorise the sequence' : 'Repeat the sequence',
                style: theme.textTheme.bodyMedium,
              ),
            ],
          ),
        ),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.all(AppSpacing.lg),
            child: GridView.builder(
              gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: 3,
                mainAxisSpacing: AppSpacing.sm,
                crossAxisSpacing: AppSpacing.sm,
                childAspectRatio: 1.2,
              ),
              itemCount: _tiles,
              itemBuilder: (context, i) {
                final lit = _highlight == i;
                return Semantics(
                  button: true,
                  label: 'Gate ${i + 1}',
                  child: GestureDetector(
                    onTap: () => _tap(i),
                    child: AnimatedContainer(
                      duration: const Duration(milliseconds: 120),
                      decoration: BoxDecoration(
                        color: lit
                            ? AppColors.secondary
                            : theme.colorScheme.surfaceContainerHighest,
                        borderRadius:
                            BorderRadius.circular(AppSpacing.cardRadius),
                      ),
                      child: Center(
                        child: Text('${i + 1}',
                            style: theme.textTheme.titleLarge?.copyWith(
                                color: lit ? Colors.black87 : null)),
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
        ),
      ],
    );
  }
}
