import os
import tempfile
from pathlib import Path
from uuid import uuid4

import pytest
import sqlalchemy as sa
from alembic import command
from alembic.config import Config


TEST_DATABASE_PATH = Path(tempfile.gettempdir()) / f"prometheus-pytest-{os.getpid()}.db"
POSTGRES_TEST_URL = os.getenv("PROMETHEUS_TEST_POSTGRES_URL")
POSTGRES_SCHEMA = f"prometheus_app_test_{uuid4().hex}"
POSTGRES_ADMIN_ENGINE: sa.Engine | None = None

if POSTGRES_TEST_URL:
    postgres_base_url = sa.engine.make_url(POSTGRES_TEST_URL)
    if postgres_base_url.drivername == "postgresql":
        postgres_base_url = postgres_base_url.set(drivername="postgresql+psycopg")
    POSTGRES_ADMIN_ENGINE = sa.create_engine(postgres_base_url)
    application_url = postgres_base_url.update_query_dict(
        {"options": f"-csearch_path={POSTGRES_SCHEMA}"}
    )
    os.environ["PROMETHEUS_DATABASE_URL"] = application_url.render_as_string(
        hide_password=False
    )
else:
    os.environ["PROMETHEUS_DATABASE_URL"] = f"sqlite:///{TEST_DATABASE_PATH}"

from app import models, models_v1, models_v2  # noqa: E402,F401
from app.database import engine  # noqa: E402


BACKEND_ROOT = Path(__file__).parents[1]


def migration_config() -> Config:
    config = Config(str(BACKEND_ROOT / "alembic.ini"))
    config.set_main_option("script_location", str(BACKEND_ROOT / "migrations"))
    config.attributes["database_url"] = os.environ[
        "PROMETHEUS_DATABASE_URL"
    ].replace("%", "%%")
    return config


def reset_database() -> None:
    engine.dispose()
    if POSTGRES_ADMIN_ENGINE is not None:
        with POSTGRES_ADMIN_ENGINE.begin() as connection:
            connection.execute(
                sa.schema.DropSchema(
                    POSTGRES_SCHEMA, cascade=True, if_exists=True
                )
            )
            connection.execute(sa.schema.CreateSchema(POSTGRES_SCHEMA))
    else:
        TEST_DATABASE_PATH.unlink(missing_ok=True)
    command.upgrade(migration_config(), "head")


def remove_database() -> None:
    engine.dispose()
    if POSTGRES_ADMIN_ENGINE is not None:
        with POSTGRES_ADMIN_ENGINE.begin() as connection:
            connection.execute(
                sa.schema.DropSchema(
                    POSTGRES_SCHEMA, cascade=True, if_exists=True
                )
            )
    else:
        TEST_DATABASE_PATH.unlink(missing_ok=True)


@pytest.fixture(autouse=True)
def isolated_database():
    reset_database()
    yield
    remove_database()


@pytest.fixture(scope="session", autouse=True)
def remove_test_database():
    yield
    remove_database()
    if POSTGRES_ADMIN_ENGINE is not None:
        POSTGRES_ADMIN_ENGINE.dispose()
