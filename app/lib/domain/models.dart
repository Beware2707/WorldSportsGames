/// Immutable domain models mirroring the backend API schemas.
///
/// Hand-written for Sprint 1 (few models, zero codegen); migrate to Freezed
/// once result/live models multiply in Sprint 2.
library;

class Paged<T> {
  const Paged({
    required this.items,
    required this.total,
    required this.page,
    required this.pages,
  });

  final List<T> items;
  final int total;
  final int page;
  final int pages;

  factory Paged.fromJson(
    Map<String, dynamic> json,
    T Function(Map<String, dynamic>) itemFromJson,
  ) =>
      Paged(
        items: (json['items'] as List)
            .map((e) => itemFromJson(e as Map<String, dynamic>))
            .toList(),
        total: json['total'] as int,
        page: json['page'] as int,
        pages: json['pages'] as int,
      );
}

class Sport {
  const Sport({
    required this.id,
    required this.code,
    required this.name,
    required this.category,
    this.icon,
    this.disciplines = const [],
  });

  final int id;
  final String code;
  final String name;
  final String category; // summer | winter | la28
  final String? icon;
  final List<Discipline> disciplines;

  factory Sport.fromJson(Map<String, dynamic> json) => Sport(
        id: json['id'] as int,
        code: json['code'] as String,
        name: json['name'] as String,
        category: json['category'] as String,
        icon: json['icon'] as String?,
        disciplines: (json['disciplines'] as List? ?? const [])
            .map((e) => Discipline.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class Discipline {
  const Discipline({required this.id, required this.code, required this.name});

  final int id;
  final String code;
  final String name;

  factory Discipline.fromJson(Map<String, dynamic> json) => Discipline(
        id: json['id'] as int,
        code: json['code'] as String,
        name: json['name'] as String,
      );
}

class Country {
  const Country({
    required this.id,
    required this.iso3,
    required this.name,
    this.flagEmoji,
  });

  final int id;
  final String iso3;
  final String name;
  final String? flagEmoji;

  factory Country.fromJson(Map<String, dynamic> json) => Country(
        id: json['id'] as int,
        iso3: json['iso3'] as String,
        name: json['name'] as String,
        flagEmoji: json['flag_emoji'] as String?,
      );
}

class Athlete {
  const Athlete({
    required this.id,
    required this.slug,
    required this.fullName,
    this.country,
    this.headshotUrl,
    this.disciplines = const [],
    this.bio,
  });

  final int id;
  final String slug;
  final String fullName;
  final Country? country;
  final String? headshotUrl;
  final List<Discipline> disciplines;
  final String? bio;

  factory Athlete.fromJson(Map<String, dynamic> json) => Athlete(
        id: json['id'] as int,
        slug: json['slug'] as String,
        fullName: json['full_name'] as String,
        country: json['country'] == null
            ? null
            : Country.fromJson(json['country'] as Map<String, dynamic>),
        headshotUrl: json['headshot_url'] as String?,
        disciplines: (json['disciplines'] as List? ?? const [])
            .map((e) => Discipline.fromJson(e as Map<String, dynamic>))
            .toList(),
        bio: json['bio'] as String?,
      );
}

class Competition {
  const Competition({
    required this.id,
    required this.slug,
    required this.name,
    required this.level,
    this.sport,
    this.editions = const [],
  });

  final int id;
  final String slug;
  final String name;
  final String level;
  final Sport? sport;
  final List<Edition> editions;

  factory Competition.fromJson(Map<String, dynamic> json) => Competition(
        id: json['id'] as int,
        slug: json['slug'] as String,
        name: json['name'] as String,
        level: json['level'] as String,
        sport: json['sport'] == null
            ? null
            : Sport.fromJson(json['sport'] as Map<String, dynamic>),
        editions: (json['editions'] as List? ?? const [])
            .map((e) => Edition.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

class Edition {
  const Edition({
    required this.id,
    required this.label,
    required this.year,
    required this.status,
    this.startDate,
    this.hostCity,
    this.hostCountry,
    this.competition,
  });

  final int id;
  final String label;
  final int year;
  final String status; // upcoming | in_progress | completed
  final DateTime? startDate;
  final String? hostCity;
  final Country? hostCountry;
  final Competition? competition;

  /// An edition spanning days or months is "in progress" — deliberately NOT a
  /// live-coverage signal. Only a live event may drive a LIVE indicator.
  bool get isInProgress => status == 'in_progress';

  factory Edition.fromJson(Map<String, dynamic> json) => Edition(
        id: json['id'] as int,
        label: json['label'] as String,
        year: json['year'] as int,
        status: json['status'] as String,
        startDate: json['start_date'] == null
            ? null
            : DateTime.parse(json['start_date'] as String),
        hostCity: json['host_city'] as String?,
        hostCountry: json['host_country'] == null
            ? null
            : Country.fromJson(json['host_country'] as Map<String, dynamic>),
        competition: json['competition'] == null
            ? null
            : Competition.fromJson(json['competition'] as Map<String, dynamic>),
      );
}

/// A server-composed home feed section; `kind` selects the client renderer.
class HomeSection {
  const HomeSection({required this.kind, required this.title, required this.items});

  final String kind;
  final String title;
  final List<Map<String, dynamic>> items;

  factory HomeSection.fromJson(Map<String, dynamic> json) => HomeSection(
        kind: json['kind'] as String,
        title: json['title'] as String,
        items: (json['items'] as List).cast<Map<String, dynamic>>(),
      );
}

class UserAccount {
  const UserAccount({required this.id, required this.email, required this.displayName});

  final int id;
  final String email;
  final String displayName;

  factory UserAccount.fromJson(Map<String, dynamic> json) => UserAccount(
        id: json['id'] as int,
        email: json['email'] as String,
        displayName: json['display_name'] as String,
      );
}
