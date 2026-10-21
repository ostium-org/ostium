"""Application settings, loaded from environment / .env file.

The Supabase values are optional here so the app can still boot (and serve
`/health`) before they are configured. Requests that actually need Supabase
raise a clear 503 if the settings are missing.
"""

from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    supabase_url: str = ""
    # service role key (Supabase's "secret key")
    supabase_secret_key: str = ""
    # publishable key (Supabase's "anon key") - optional for the backend
    supabase_publishable_key: str = ""

    cors_origins: str = "*"

    @property
    def cors_origin_list(self) -> list[str]:
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]


@lru_cache
def get_settings() -> Settings:
    return Settings()
