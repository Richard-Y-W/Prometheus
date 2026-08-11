from pydantic_settings import BaseSettings, SettingsConfigDict

class Settings(BaseSettings):
    database_url: str = "sqlite:///./prometheus.db"
    max_upload_mb: int = 50
    llm_provider: str = "mock"
    model_config = SettingsConfigDict(env_prefix="PROMETHEUS_")

settings = Settings()
