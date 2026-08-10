import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/network/api_client.dart';
import '../../domain/personalization_models.dart';
import '../profile/profile_screen.dart';
import 'follows_controller.dart';

/// Follow/unfollow control used on every followable entity.
///
/// Signed-out users see the control and are told to sign in when they tap it,
/// rather than the affordance being hidden — hiding it makes the app look like
/// it lacks the feature.
class FollowButton extends ConsumerWidget {
  const FollowButton({
    super.key,
    required this.kind,
    required this.entityId,
    this.name,
    this.compact = false,
  });

  final FollowKind kind;
  final int entityId;
  final String? name;
  final bool compact;

  Future<void> _onPressed(BuildContext context, WidgetRef ref) async {
    final messenger = ScaffoldMessenger.of(context);
    if (ref.read(currentUserProvider) == null) {
      messenger.showSnackBar(
        const SnackBar(content: Text('Sign in from Profile to follow')),
      );
      return;
    }
    try {
      await ref
          .read(followsProvider.notifier)
          .toggle(kind, entityId, name: name);
    } on ApiException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final following = ref.watch(isFollowingProvider((kind, entityId)));
    final label = following ? 'Following' : 'Follow';
    final icon = following ? Icons.check_rounded : Icons.add_rounded;

    if (compact) {
      return IconButton(
        onPressed: () => _onPressed(context, ref),
        icon: Icon(icon),
        tooltip: label,
        isSelected: following,
        color: following ? Theme.of(context).colorScheme.primary : null,
      );
    }
    return Semantics(
      button: true,
      label: '$label ${name ?? kind.name}',
      child: following
          ? OutlinedButton.icon(
              onPressed: () => _onPressed(context, ref),
              icon: Icon(icon, size: 18),
              label: Text(label),
            )
          : FilledButton.icon(
              onPressed: () => _onPressed(context, ref),
              icon: Icon(icon, size: 18),
              label: Text(label),
            ),
    );
  }
}
