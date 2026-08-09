/// Event, result and live-coverage models mirroring the backend schemas.
library;

import 'models.dart';

class SportEvent {
  const SportEvent({
    required this.id,
    required this.name,
    required this.gender,
    required this.phase,
    required this.status,
    this.scheduledStart,
    this.discipline,
  });

  final int id;
  final String name;
  final String gender; // M / F / X
  final String phase;
  final String status; // scheduled | live | completed | cancelled
  final DateTime? scheduledStart;
  final Discipline? discipline;

  bool get isLive => status == 'live';

  factory SportEvent.fromJson(Map<String, dynamic> json) => SportEvent(
        id: json['id'] as int,
        name: json['name'] as String,
        gender: json['gender'] as String,
        phase: json['phase'] as String,
        status: json['status'] as String,
        scheduledStart: json['scheduled_start'] == null
            ? null
            : DateTime.parse(json['scheduled_start'] as String),
        discipline: json['discipline'] == null
            ? null
            : Discipline.fromJson(json['discipline'] as Map<String, dynamic>),
      );
}

/// One athlete's line in an event's result sheet.
class EventResult {
  const EventResult({
    required this.athlete,
    this.lane,
    this.bib,
    this.position,
    this.status,
    this.valueKind,
    this.valueText,
    this.qualified,
  });

  final Athlete athlete;
  final int? lane;
  final String? bib;
  final int? position;
  final String? status; // ok | DNS | DNF | DSQ | null (no result yet)
  final String? valueKind; // time | distance | height | points | score | ...
  final String? valueText;
  final bool? qualified;

  factory EventResult.fromJson(Map<String, dynamic> json) => EventResult(
        athlete: Athlete.fromJson(json['athlete'] as Map<String, dynamic>),
        lane: json['lane'] as int?,
        bib: json['bib'] as String?,
        position: json['position'] as int?,
        status: json['status'] as String?,
        valueKind: json['value_kind'] as String?,
        valueText: json['value_text'] as String?,
        qualified: json['qualified'] as bool?,
      );
}

class SportEventDetail extends SportEvent {
  const SportEventDetail({
    required super.id,
    required super.name,
    required super.gender,
    required super.phase,
    required super.status,
    super.scheduledStart,
    super.discipline,
    required this.competitionName,
    required this.competitionSlug,
    this.edition,
    this.results = const [],
  });

  final String competitionName;
  final String competitionSlug;
  final Edition? edition;
  final List<EventResult> results;

  factory SportEventDetail.fromJson(Map<String, dynamic> json) {
    final base = SportEvent.fromJson(json);
    return SportEventDetail(
      id: base.id,
      name: base.name,
      gender: base.gender,
      phase: base.phase,
      status: base.status,
      scheduledStart: base.scheduledStart,
      discipline: base.discipline,
      competitionName: json['competition_name'] as String,
      competitionSlug: json['competition_slug'] as String,
      edition: json['edition'] == null
          ? null
          : Edition.fromJson(json['edition'] as Map<String, dynamic>),
      results: (json['results'] as List? ?? const [])
          .map((e) => EventResult.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }
}

/// An event with genuine live coverage (from GET /live or the WS snapshot).
class LiveCoverage {
  const LiveCoverage({
    required this.event,
    required this.editionLabel,
    required this.competitionName,
    required this.competitionSlug,
    this.currentPhase,
    required this.lastSeq,
  });

  final SportEvent event;
  final String editionLabel;
  final String competitionName;
  final String competitionSlug;
  final String? currentPhase;
  final int lastSeq;

  factory LiveCoverage.fromJson(Map<String, dynamic> json) => LiveCoverage(
        event: SportEvent.fromJson(json['event'] as Map<String, dynamic>),
        editionLabel: json['edition_label'] as String,
        competitionName: json['competition_name'] as String,
        competitionSlug: json['competition_slug'] as String,
        currentPhase: json['current_phase'] as String?,
        lastSeq: json['last_seq'] as int,
      );
}

/// A diff frame from the live WebSocket stream.
class LiveUpdateFrame {
  const LiveUpdateFrame({
    required this.eventId,
    required this.seq,
    required this.kind,
    required this.payload,
  });

  final int eventId;
  final int seq;
  final String kind; // status | progress | results | note
  final Map<String, dynamic> payload;

  factory LiveUpdateFrame.fromJson(Map<String, dynamic> json) => LiveUpdateFrame(
        eventId: json['event_id'] as int,
        seq: json['seq'] as int,
        kind: json['kind'] as String,
        payload: (json['payload'] as Map?)?.cast<String, dynamic>() ?? const {},
      );
}
