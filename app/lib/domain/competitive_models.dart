/// Rankings, records, medals and profile models.
library;

import 'models.dart';

class RankingEntry {
  const RankingEntry({
    required this.rank,
    required this.entityId,
    this.points,
    this.entityName,
    this.entitySubtitle,
    this.entitySlug,
  });

  final int rank;
  final int entityId;
  final double? points;
  final String? entityName;
  final String? entitySubtitle;
  final String? entitySlug;

  factory RankingEntry.fromJson(Map<String, dynamic> json) => RankingEntry(
        rank: json['rank'] as int,
        entityId: json['entity_id'] as int,
        points: (json['points'] as num?)?.toDouble(),
        entityName: json['entity_name'] as String?,
        entitySubtitle: json['entity_subtitle'] as String?,
        entitySlug: json['entity_slug'] as String?,
      );
}

class RankingLadder {
  const RankingLadder({
    required this.scope,
    required this.methodology,
    required this.entries,
    this.discipline,
    this.asOf,
  });

  final String scope;
  final String methodology;
  final List<RankingEntry> entries;
  final Discipline? discipline;
  final DateTime? asOf;

  factory RankingLadder.fromJson(Map<String, dynamic> json) => RankingLadder(
        scope: json['scope'] as String,
        methodology: json['methodology'] as String,
        discipline: json['discipline'] == null
            ? null
            : Discipline.fromJson(json['discipline'] as Map<String, dynamic>),
        asOf: json['as_of'] == null
            ? null
            : DateTime.parse(json['as_of'] as String),
        entries: (json['entries'] as List)
            .map((e) => RankingEntry.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class SportRecord {
  const SportRecord({
    required this.id,
    required this.kind,
    required this.eventName,
    required this.gender,
    required this.valueText,
    required this.valueKind,
    this.unit,
    this.setOn,
    this.location,
    this.discipline,
    this.country,
    this.holderName,
    this.holderSlug,
  });

  final int id;
  final String kind; // WR | OR | CR | AR | NR | PB | SB
  final String eventName;
  final String gender;
  final String valueText;
  final String valueKind;
  final String? unit;
  final DateTime? setOn;
  final String? location;
  final Discipline? discipline;
  final Country? country;
  final String? holderName;
  final String? holderSlug;

  factory SportRecord.fromJson(Map<String, dynamic> json) => SportRecord(
        id: json['id'] as int,
        kind: json['kind'] as String,
        eventName: json['event_name'] as String,
        gender: json['gender'] as String,
        valueText: json['value_text'] as String,
        valueKind: json['value_kind'] as String,
        unit: json['unit'] as String?,
        setOn: json['set_on'] == null
            ? null
            : DateTime.parse(json['set_on'] as String),
        location: json['location'] as String?,
        discipline: json['discipline'] == null
            ? null
            : Discipline.fromJson(json['discipline'] as Map<String, dynamic>),
        country: json['country'] == null
            ? null
            : Country.fromJson(json['country'] as Map<String, dynamic>),
        holderName: json['holder_name'] as String?,
        holderSlug: json['holder_slug'] as String?,
      );

  String get kindLabel => switch (kind) {
        'WR' => 'World record',
        'OR' => 'Olympic record',
        'CR' => 'Competition record',
        'AR' => 'Area record',
        'NR' => 'National record',
        'PB' => 'Personal best',
        'SB' => 'Season best',
        _ => kind,
      };
}

class MedalTally {
  const MedalTally({
    required this.country,
    required this.gold,
    required this.silver,
    required this.bronze,
    required this.total,
    required this.rank,
  });

  final Country country;
  final int gold;
  final int silver;
  final int bronze;
  final int total;
  final int rank;

  factory MedalTally.fromJson(Map<String, dynamic> json) => MedalTally(
        country: Country.fromJson(json['country'] as Map<String, dynamic>),
        gold: json['gold'] as int,
        silver: json['silver'] as int,
        bronze: json['bronze'] as int,
        total: json['total'] as int,
        rank: json['rank'] as int,
      );
}

class MedalTable {
  const MedalTable({required this.rows, this.editionId, this.editionLabel});

  final List<MedalTally> rows;
  final int? editionId;
  final String? editionLabel;

  factory MedalTable.fromJson(Map<String, dynamic> json) => MedalTable(
        editionId: json['edition_id'] as int?,
        editionLabel: json['edition_label'] as String?,
        rows: (json['rows'] as List)
            .map((e) => MedalTally.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class CountryProfile {
  const CountryProfile({
    required this.country,
    required this.athleteCount,
    required this.athletes,
    required this.records,
    this.medals,
  });

  final Country country;
  final int athleteCount;
  final List<Athlete> athletes;
  final List<SportRecord> records;
  final MedalTally? medals;

  factory CountryProfile.fromJson(Map<String, dynamic> json) => CountryProfile(
        country: Country.fromJson(json['country'] as Map<String, dynamic>),
        athleteCount: json['athlete_count'] as int,
        medals: json['medals'] == null
            ? null
            : MedalTally.fromJson(json['medals'] as Map<String, dynamic>),
        athletes: (json['athletes'] as List)
            .map((e) => Athlete.fromJson(e as Map<String, dynamic>))
            .toList(),
        records: (json['records'] as List)
            .map((e) => SportRecord.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class AthleteMedal {
  const AthleteMedal({
    required this.metal,
    required this.eventName,
    this.editionLabel,
    this.competitionName,
    this.discipline,
  });

  final String metal;
  final String eventName;
  final String? editionLabel;
  final String? competitionName;
  final String? discipline;

  factory AthleteMedal.fromJson(Map<String, dynamic> json) => AthleteMedal(
        metal: json['metal'] as String,
        eventName: json['event_name'] as String,
        editionLabel: json['edition_label'] as String?,
        competitionName: json['competition_name'] as String?,
        discipline: json['discipline'] as String?,
      );
}

class AthleteResultLine {
  const AthleteResultLine({
    required this.eventId,
    required this.eventName,
    this.competitionName,
    this.position,
    this.valueText,
    this.resultStatus,
  });

  final int eventId;
  final String eventName;
  final String? competitionName;
  final int? position;
  final String? valueText;
  final String? resultStatus;

  factory AthleteResultLine.fromJson(Map<String, dynamic> json) =>
      AthleteResultLine(
        eventId: json['event_id'] as int,
        eventName: json['event_name'] as String,
        competitionName: json['competition_name'] as String?,
        position: json['position'] as int?,
        valueText: json['value_text'] as String?,
        resultStatus: json['result_status'] as String?,
      );
}

class AthleteRankingLine {
  const AthleteRankingLine({
    required this.methodology,
    required this.rank,
    this.points,
    this.discipline,
  });

  final String methodology;
  final int rank;
  final double? points;
  final String? discipline;

  factory AthleteRankingLine.fromJson(Map<String, dynamic> json) =>
      AthleteRankingLine(
        methodology: json['methodology'] as String,
        rank: json['rank'] as int,
        points: (json['points'] as num?)?.toDouble(),
        discipline: json['discipline'] as String?,
      );
}

class AthleteProfile {
  const AthleteProfile({
    required this.athlete,
    required this.disciplines,
    required this.personalBests,
    required this.medals,
    required this.recentResults,
    required this.rankings,
  });

  final Athlete athlete;
  final List<Discipline> disciplines;
  final List<SportRecord> personalBests;
  final List<AthleteMedal> medals;
  final List<AthleteResultLine> recentResults;
  final List<AthleteRankingLine> rankings;

  factory AthleteProfile.fromJson(Map<String, dynamic> json) => AthleteProfile(
        athlete: Athlete.fromJson(json['athlete'] as Map<String, dynamic>),
        disciplines: (json['disciplines'] as List)
            .map((e) => Discipline.fromJson(e as Map<String, dynamic>))
            .toList(),
        personalBests: (json['personal_bests'] as List)
            .map((e) => SportRecord.fromJson(e as Map<String, dynamic>))
            .toList(),
        medals: (json['medals'] as List)
            .map((e) => AthleteMedal.fromJson(e as Map<String, dynamic>))
            .toList(),
        recentResults: (json['recent_results'] as List)
            .map((e) => AthleteResultLine.fromJson(e as Map<String, dynamic>))
            .toList(),
        rankings: (json['world_rankings'] as List)
            .map((e) => AthleteRankingLine.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}
