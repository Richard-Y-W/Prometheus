import os
import tempfile
from pathlib import Path

import pytest
from alembic import command
from alembic.config import Config


TEST_DATABASE_PATH = Path(tempfile.gettempdir()) / f"prometheus-pytest-{os.getpid()}.db"
os.environ["PROMETHEUS_DATABASE_URL"] = f"sqlite:///{TEST_DATABASE_PATH}"

from app import models, models_v1, models_v2  # noqa: E402,F401
from app.database import engine  # noqa: E402


BACKEND_ROOT = Path(__file__).parents[1]


def migration_config() -> Config:
    config = Config(str(BACKEND_ROOT / "alembic.ini"))
    config.set_main_option("script_location", str(BACKEND_ROOT / "migrations"))
    config.attributes["database_url"] = f"sqlite:///{TEST_DATABASE_PATH}"
    return config


@pytest.fixture(autouse=True)
def isolated_database():
    engine.dispose()
    TEST_DATABASE_PATH.unlink(missing_ok=True)
    command.upgrade(migration_config(), "head")
    yield
    engine.dispose()
    TEST_DATABASE_PATH.unlink(missing_ok=True)


@pytest.fixture(scope="session", autouse=True)
def remove_test_database():
    yield
    engine.dispose()
    TEST_DATABASE_PATH.unlink(missing_ok=True)
