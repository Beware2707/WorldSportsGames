import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../data/repositories.dart';
import '../../domain/models.dart';

/// retry: null — failures surface immediately; the UI offers manual retry.
final homeFeedProvider = FutureProvider.autoDispose<List<HomeSection>>(
  (ref) => ref.watch(catalogRepositoryProvider).homeFeed(),
  retry: (count, error) => null,
);
