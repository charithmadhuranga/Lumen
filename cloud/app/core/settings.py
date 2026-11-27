"""Settings from the environment only (prefix LUMEN_). See .env.example.

Production refuses to start with a missing or development-default secret, a
non-TLS broker URL or debug features on (p01-cloud-foundation.md).
"""

from __future__ import annotations

from functools import lru_cache
from typing import Literal

from pydantic import Field, SecretStr, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

# Values that exist only so a development checkout starts. Production rejects them.
DEV_SECRET = "dev-insecure-change-me-0123456789abcdef"  # noqa: S105 - deliberate development placeholder


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="LUMEN_", env_file=None, extra="ignore")

    environment: Literal["development", "test", "production"] = "development"

    # Public API (apps) and internal API (broker hooks, metrics) listen separately.
    public_host: str = "0.0.0.0"  # noqa: S104 - container listens on all interfaces
    public_port: int = Field(default=8800, ge=1, le=65535)
    internal_host: str = "0.0.0.0"  # noqa: S104
    internal_port: int = Field(default=8801, ge=1, le=65535)

    # Proxies whose X-Forwarded-For is trusted for the client IP (rate limits).
    # Comma-separated IPs/CIDRs; "*" only behind a load balancer that strips the header.
    trusted_proxies: str = "127.0.0.1"

    database_url: SecretStr = SecretStr("postgresql+asyncpg://lumen:lumen@localhost:55432/lumen")
    redis_url: SecretStr = SecretStr("redis://localhost:56379/0")
    broker_url: str = "mqtts://localhost:28883"  # given to gateways at registration
    # EMQX management API (kick revoked gateways). Empty URL disables kicking.
    emqx_api_url: str = "http://localhost:28083"
    emqx_api_key: str = "lumen-cloud"
    emqx_api_secret: SecretStr = SecretStr(DEV_SECRET)
    # Password of the cloud's own broker clients (svc:*), P4.
    mqtt_service_password: SecretStr = SecretStr(DEV_SECRET)

    jwt_secret: SecretStr = SecretStr(DEV_SECRET)
    jwt_previous_secret: SecretStr | None = None  # still accepted for verification during key rotation
    hook_secret: SecretStr = SecretStr(DEV_SECRET)  # EMQX -> /internal/mqtt/* (P3)
    kek: SecretStr = SecretStr(DEV_SECRET)  # wraps per-controller K_local (P3)

    # Rate limits (P2). Windows in seconds.
    claim_ttl_s: int = Field(default=900, ge=60, le=3600)
    register_device_ip_limit: int = Field(default=20, ge=1)  # gateway/children registrations per IP per hour
    login_fail_limit: int = Field(default=5, ge=1)  # per IP + e-mail
    login_ip_fail_limit: int = Field(default=30, ge=1)  # per IP, all e-mails (spraying)
    login_window_s: int = Field(default=300, ge=1)
    lockout_after: int = Field(default=10, ge=1)  # consecutive failures on one account
    lockout_s: int = Field(default=300, ge=1)
    register_ip_limit: int = Field(default=10, ge=1)
    register_window_s: int = Field(default=3600, ge=1)
    refresh_ttl_days: int = Field(default=30, ge=1, le=365)

    log_level: Literal["DEBUG", "INFO", "WARNING", "ERROR"] = "INFO"
    log_json: bool = True
    db_pool_size: int = Field(default=10, ge=1, le=100)
    readiness_timeout_s: float = Field(default=2.0, gt=0, le=30)

    @model_validator(mode="after")
    def _production_is_safe(self) -> Settings:
        if self.environment != "production":
            return self
        problems: list[str] = []
        for name in ("jwt_secret", "hook_secret", "kek", "emqx_api_secret", "mqtt_service_password"):
            value = getattr(self, name).get_secret_value()
            if value == DEV_SECRET or len(value) < 32:
                problems.append(f"LUMEN_{name.upper()} must be set to a random value of at least 32 characters")
        if "lumen:lumen@" in self.database_url.get_secret_value():
            problems.append("LUMEN_DATABASE_URL uses the development credentials")
        if not self.broker_url.startswith("mqtts://"):
            problems.append("LUMEN_BROKER_URL must use mqtts://")
        if self.log_level == "DEBUG":
            problems.append("LUMEN_LOG_LEVEL=DEBUG is not allowed in production")
        if problems:
            raise ValueError("unsafe production settings: " + "; ".join(problems))
        return self


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    return Settings()
