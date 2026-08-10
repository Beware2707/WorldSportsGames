import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/widgets/common.dart';
import '../../data/personalization_repository.dart';
import '../../domain/personalization_models.dart';

/// Debounced autocomplete. Keystrokes coalesce so typing does not fire a
/// request per character; an empty query resolves to no results without a
/// round-trip.
final suggestionsProvider =
    FutureProvider.autoDispose.family<List<SearchSuggestion>, String>(
  (ref, query) async {
    if (query.trim().isEmpty) return const [];
    // Cancel-on-rekey: a pending debounce is discarded when the query changes.
    var active = true;
    ref.onDispose(() => active = false);
    await Future<void>.delayed(const Duration(milliseconds: 250));
    if (!active) return const [];
    return ref.watch(personalizationRepositoryProvider).suggest(query.trim());
  },
  retry: (count, error) => null,
);

class SearchScreen extends ConsumerStatefulWidget {
  const SearchScreen({super.key});

  @override
  ConsumerState<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends ConsumerState<SearchScreen> {
  final _controller = TextEditingController();
  String _query = '';

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _open(SearchSuggestion suggestion) {
    final slug = suggestion.slug;
    if (slug == null) return;
    switch (suggestion.kind) {
      case 'sport':
        context.go('/sports/$slug');
      case 'competition':
        context.push('/competitions/$slug');
      case 'athlete':
        context.push('/athletes/$slug');
      case 'country':
        context.push('/countries/$slug');
    }
  }

  @override
  Widget build(BuildContext context) {
    final suggestions = ref.watch(suggestionsProvider(_query));
    return Scaffold(
      appBar: AppBar(
        title: TextField(
          controller: _controller,
          autofocus: true,
          textInputAction: TextInputAction.search,
          decoration: InputDecoration(
            hintText: 'Search sports, athletes, competitions…',
            border: InputBorder.none,
            suffixIcon: _query.isEmpty
                ? null
                : IconButton(
                    icon: const Icon(Icons.clear_rounded),
                    tooltip: 'Clear',
                    onPressed: () {
                      _controller.clear();
                      setState(() => _query = '');
                    },
                  ),
          ),
          onChanged: (value) => setState(() => _query = value),
        ),
      ),
      body: _query.trim().isEmpty
          ? const EmptyState(
              icon: Icons.search_rounded,
              title: 'Search the platform',
              message: 'Find any sport, athlete, country or competition.',
            )
          : suggestions.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (e, _) => ErrorState(
                message: e.toString(),
                onRetry: () => ref.invalidate(suggestionsProvider(_query)),
              ),
              data: (items) => items.isEmpty
                  ? EmptyState(
                      icon: Icons.search_off_rounded,
                      title: 'No matches for "${_query.trim()}"',
                      message: 'Try a different spelling or a broader term.',
                    )
                  : ListView.builder(
                      itemCount: items.length,
                      itemBuilder: (context, i) {
                        final item = items[i];
                        return ListTile(
                          leading: Icon(_iconFor(item.kind)),
                          title: Text(item.label),
                          subtitle: Text([
                            _kindLabel(item.kind),
                            if (item.sublabel != null) item.sublabel!,
                          ].join(' · ')),
                          trailing: const Icon(Icons.chevron_right_rounded),
                          onTap: () => _open(item),
                        );
                      },
                    ),
            ),
    );
  }
}

IconData _iconFor(String kind) => switch (kind) {
      'sport' => Icons.emoji_events_outlined,
      'athlete' => Icons.person_outline_rounded,
      'competition' => Icons.calendar_month_outlined,
      'country' => Icons.flag_outlined,
      _ => Icons.search_rounded,
    };

String _kindLabel(String kind) => switch (kind) {
      'sport' => 'Sport',
      'athlete' => 'Athlete',
      'competition' => 'Competition',
      'country' => 'Country',
      _ => kind,
    };

/// Search entry point for app bars.
class SearchIconButton extends StatelessWidget {
  const SearchIconButton({super.key});

  @override
  Widget build(BuildContext context) {
    return IconButton(
      icon: const Icon(Icons.search_rounded),
      tooltip: 'Search',
      onPressed: () => context.push('/search'),
    );
  }
}
