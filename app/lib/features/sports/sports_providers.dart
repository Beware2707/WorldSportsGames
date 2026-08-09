import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../data/repositories.dart';
import '../../domain/models.dart';

/// null category = all sports. retry: null — failures surface immediately;
/// the UI offers manual retry.
final sportsProvider =
    FutureProvider.autoDispose.family<List<Sport>, String?>(
  (ref, category) =>
      ref.watch(catalogRepositoryProvider).listSports(category: category),
  retry: (count, error) => null,
);

final sportDetailProvider = FutureProvider.autoDispose.family<Sport, String>(
  (ref, code) => ref.watch(catalogRepositoryProvider).getSport(code),
  retry: (count, error) => null,
);
