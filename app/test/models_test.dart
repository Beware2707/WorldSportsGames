import 'package:flutter_test/flutter_test.dart';
import 'package:world_sports_games/domain/models.dart';

void main() {
  test('Paged parses envelope and items', () {
    final page = Paged.fromJson({
      'items': [
        {'id': 1, 'code': 'judo', 'name': 'Judo', 'category': 'summer',
          'icon': null},
      ],
      'total': 54,
      'page': 1,
      'size': 20,
      'pages': 3,
    }, Sport.fromJson);
    expect(page.total, 54);
    expect(page.items.single.code, 'judo');
    expect(page.items.single.disciplines, isEmpty);
  });

  test('Edition parses nested competition and live status', () {
    final edition = Edition.fromJson({
      'id': 8,
      'label': '2026 Season',
      'year': 2026,
      'status': 'live',
      'start_date': '2026-04-25',
      'host_city': null,
      'host_country': null,
      'competition': {
        'id': 5,
        'slug': 'diamond-league',
        'name': 'Diamond League',
        'level': 'league',
        'sport': {'id': 2, 'code': 'athletics', 'name': 'Athletics',
          'category': 'summer', 'icon': 'sprint'},
      },
    });
    expect(edition.isLive, isTrue);
    expect(edition.competition?.sport?.code, 'athletics');
    expect(edition.startDate?.year, 2026);
  });

  test('Athlete tolerates missing optional fields', () {
    final athlete = Athlete.fromJson({
      'id': 1,
      'slug': 'x',
      'full_name': 'X Y',
      'country': null,
      'headshot_url': null,
    });
    expect(athlete.country, isNull);
    expect(athlete.disciplines, isEmpty);
  });
}
