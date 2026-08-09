from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="SPORTS_", env_file=".env", extra="ignore")

    app_name: str = "World Athletics & Sports Games"
    debug: bool = False

    database_url: str = "postgresql+asyncpg://sports:sports@localhost:5434/sportsdb"
    # Empty redis_url disables caching and live pub/sub (local-only fan-out).
    redis_url: str = "redis://localhost:6381/0"
    cache_ttl_seconds: int = 300

    # Dev default only (>=32 bytes for HS256) — every deployed environment
    # MUST set SPORTS_JWT_SECRET.
    jwt_secret: str = "dev-insecure-change-me-dev-insecure-change-me"
    jwt_algorithm: str = "HS256"
    jwt_expires_minutes: int = 60 * 24

    # Dev origins for the Flutter web client; set explicitly in production.
    cors_origins: list[str] = [
        "http://localhost",
        "http://localhost:3000",
        "http://localhost:5000",
        "http://localhost:8080",
        "http://127.0.0.1:5000",
    ]

    # Gate for development fixture data (fictional athletes etc.). The static
    # sport/discipline taxonomy is real reference data and is always seedable.
    enable_dev_fixtures: bool = False


@lru_cache
def get_settings() -> Settings:
    return Settings()
