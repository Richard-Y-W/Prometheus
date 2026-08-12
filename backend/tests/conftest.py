import os
import tempfile
from pathlib import Path

import pytest


TEST_DATABASE_PATH = Path(tempfile.gettempdir()) / f"prometheus-pytest-{os.getpid()}.db"
os.environ["PROMETHEUS_DATABASE_URL"] = f"sqlite:///{TEST_DATABASE_PATH}"

from app import models, models_v1  # noqa: E402,F401
from app.database import Base, engine  # noqa: E402


@pytest.fixture(autouse=True)
def isolated_database():
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)
    yield
    Base.metadata.drop_all(engine)


@pytest.fixture(scope="session", autouse=True)
def remove_test_database():
    yield
    engine.dispose()
    TEST_DATABASE_PATH.unlink(missing_ok=True)
