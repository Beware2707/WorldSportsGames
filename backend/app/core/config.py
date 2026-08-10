from functools import lru_cache

from pydantic import model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

DEFAULT_DEV_JWT_SECRET = "dev-insecure-change-me-dev-insecure-change-me"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="SPORTS_", env_file=".env", extra="ignore")

    app_name: str = "World Athletics & Sports Games"
    debug: bool = False

    database_url: str = "postgresql+asyncpg://sports:sports@localhost:5434/sportsdb"
    # Empty redis_url disables caching and live pub/sub (local-only fan-out).
    redis_url: str = "redis://localhost:6381/0"
    cache_ttl_seconds: int = 300

    # Dev default only (>=32 bytes for HS256) — every deployed environment
    # MUST set SPORTS_JWT_SECRET. Enforced below.
    jwt_secret: str = DEFAULT_DEV_JWT_SECRET
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

    @model_validator(mode="after")
    def _reject_insecure_production_config(self) -> "Settings":
        """Fail fast rather than silently signing tokens anyone can forge.

        docker-compose already requires SPORTS_JWT_SECRET, but the backend is
        routinely started other ways (bare uvicorn, plain docker run, k8s) —
        those paths must not fall back to the committed dev secret.
        """
        if not self.debug:
            if self.jwt_secret == DEFAULT_DEV_JWT_SECRET:
                raise ValueError(
                    "SPORTS_JWT_SECRET must be set to a unique secret when "
                    "SPORTS_DEBUG is false (refusing to use the public dev default)."
                )
            if self.enable_dev_fixtures:
                raise ValueError(
                    "SPORTS_ENABLE_DEV_FIXTURES must be false when SPORTS_DEBUG "
                    "is false (fictional data must never reach production)."
                )
        return self


@lru_cache
def get_settings() -> Settings:
    return Settings()
