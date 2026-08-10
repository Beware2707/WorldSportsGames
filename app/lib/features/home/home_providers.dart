import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../data/repositories.dart';
import '../../domain/models.dart';
import '../follows/follows_controller.dart';
import '../profile/profile_screen.dart';

/// The home feed is composed server-side and personalized from the caller's
/// token, so it must refetch when the signed-in user changes or their follows
/// change — otherwise "Your Sports" would lag a session behind.
///
/// retry: null — failures surface immediately; the UI offers manual retry.
final homeFeedProvider = FutureProvider.autoDispose<List<HomeSection>>(
  (ref) {
    ref.watch(currentUserProvider);
    ref.watch(followsProvider);
    return ref.watch(catalogRepositoryProvider).homeFeed();
  },
  retry: (count, error) => null,
);
