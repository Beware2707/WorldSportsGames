import 'dart:async';
import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../core/config/app_config.dart';
import '../core/network/api_client.dart';
import '../domain/event_models.dart';
import '../domain/models.dart';
import 'repositories.dart';

/// Events and live coverage reads.
abstract class EventsRepository {
  Future<Paged<SportEvent>> listEvents({int? editionId, String? status, int page});
  Future<SportEventDetail> eventDetail(int id);
  Future<List<LiveCoverage>> liveEvents();
}

class ApiEventsRepository implements EventsRepository {
  ApiEventsRepository(this._client);

  final ApiClient _client;

  @override
  Future<Paged<SportEvent>> listEvents(
      {int? editionId, String? status, int page = 1}) async {
    final json = await _client.getJson('/api/v1/events', query: {
      'page': page,
      'size': 50,
      'edition_id': ?editionId,
      'status': ?status,
    });
    return Paged.fromJson(json, SportEvent.fromJson);
  }

  @override
  Future<SportEventDetail> eventDetail(int id) async =>
      SportEventDetail.fromJson(await _client.getJson('/api/v1/events/$id'));

  @override
  Future<List<LiveCoverage>> liveEvents() async {
    final json = await _client.getJsonList('/api/v1/live');
    return json
        .map((e) => LiveCoverage.fromJson(e as Map<String, dynamic>))
        .toList();
  }
}

final eventsRepositoryProvider = Provider<EventsRepository>(
  (ref) => ApiEventsRepository(ref.watch(apiClientProvider)),
);

/// Messages the live socket surfaces to the UI.
sealed class LiveSocketMessage {
  const LiveSocketMessage();
}

class LiveSnapshot extends LiveSocketMessage {
  const LiveSnapshot(this.events);

  final List<LiveCoverage> events;
}

class LiveDiff extends LiveSocketMessage {
  const LiveDiff(this.frame);

  final LiveUpdateFrame frame;
}

/// Thin wrapper over the live WebSocket. The stream emits a [LiveSnapshot]
/// first, then [LiveDiff]s until the socket closes.
class LiveSocket {
  LiveSocket({String? wsUrl})
      : _wsUrl = wsUrl ??
            '${AppConfig.apiBaseUrl.replaceFirst('http', 'ws')}/api/v1/live/ws';

  final String _wsUrl;
  WebSocketChannel? _channel;

  Stream<LiveSocketMessage> connect() {
    close(); // never leak a previous channel on reconnect
    final channel = WebSocketChannel.connect(Uri.parse(_wsUrl));
    _channel = channel;
    return channel.stream.map((raw) {
      final json = jsonDecode(raw as String) as Map<String, dynamic>;
      if (json['type'] == 'snapshot') {
        return LiveSnapshot(
          (json['events'] as List)
              .map((e) => LiveCoverage.fromJson(e as Map<String, dynamic>))
              .toList(),
        );
      }
      return LiveDiff(LiveUpdateFrame.fromJson(json));
    });
  }

  void close() {
    _channel?.sink.close();
    _channel = null;
  }
}

final liveSocketProvider = Provider<LiveSocket>((ref) {
  final socket = LiveSocket();
  ref.onDispose(socket.close);
  return socket;
});
