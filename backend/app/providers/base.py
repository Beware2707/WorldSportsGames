"""External sports-data provider abstraction.

Every external feed (federation API, results service, live scoring vendor)
integrates through this interface. Adapters translate provider-specific
payloads into the normalized dicts below; nothing downstream (services,
API, frontend) may ever see a raw provider response.

Sprint 1 ships the interface and registry only. Concrete adapters
(AthleticsProvider, OlympicProvider, …) arrive with ingestion workers in
Sprint 2.
"""

from typing import Any, Protocol, runtime_checkable

# Normalized payloads use plain dicts keyed to internal domain fields
# (see DATABASE.md) until ingestion lands typed DTOs in Sprint 2.
NormalizedRecord = dict[str, Any]


@runtime_checkable
class SportsDataProvider(Protocol):
    """Contract all provider adapters implement."""

    source_id: str

    async def fetch_competitions(self) -> list[NormalizedRecord]: ...

    async def fetch_athletes(self, sport_code: str | None = None) -> list[NormalizedRecord]: ...

    async def fetch_results(self, edition_ref: str) -> list[NormalizedRecord]: ...

    async def fetch_live_events(self) -> list[NormalizedRecord]: ...


_registry: dict[str, SportsDataProvider] = {}


def register_provider(provider: SportsDataProvider) -> None:
    _registry[provider.source_id] = provider


def get_provider(source_id: str) -> SportsDataProvider | None:
    return _registry.get(source_id)


def all_providers() -> list[SportsDataProvider]:
    return list(_registry.values())
