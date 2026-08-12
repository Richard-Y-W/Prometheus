import json
from decimal import Decimal
from pathlib import Path

import pytest
import sqlalchemy as sa
from alembic import command
from alembic.config import Config
from sqlalchemy.exc import IntegrityError

from app.database import SessionLocal, engine
from app.models import Project
from app.models_v1 import Component


BACKEND_ROOT = Path(__file__).parents[1]
BASE_REVISION = "cd418805b2c6"
HEAD_REVISION = "a41f0c93e2d7"
STAMP = "2026-08-11T00:00:00+00:00"


def migration_config(database_url: str) -> Config:
    config = Config(str(BACKEND_ROOT / "alembic.ini"))
    config.set_main_option("script_location", str(BACKEND_ROOT / "migrations"))
    config.attributes["database_url"] = database_url
    return config


def seed_legacy_component(database_url: str) -> None:
    migration_engine = sa.create_engine(database_url)
    with migration_engine.begin() as connection:
        connection.execute(
            sa.text(
                "INSERT INTO manufacturers "
                "(name, normalized_name, id, created_at, updated_at) "
                "VALUES (:name, :normalized_name, :id, :created_at, :updated_at)"
            ),
            {
                "name": "Prometheus Fixture Works",
                "normalized_name": "prometheusfixtureworks",
                "id": "manufacturer-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO components "
                "(manufacturer_id, part_number, normalized_part_number, family, "
                "model_class, id, created_at, updated_at) "
                "VALUES (:manufacturer_id, :part_number, :normalized_part_number, "
                ":family, :model_class, :id, :created_at, :updated_at)"
            ),
            {
                "manufacturer_id": "manufacturer-1",
                "part_number": "PM-36-GM",
                "normalized_part_number": "pm36gm",
                "family": "synthetic gearmotor",
                "model_class": "gearmotor",
                "id": "component-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO component_revisions "
                "(component_id, revision, parent_revision_id, certification_tier, "
                "status, published_at, content_hash, id, created_at, updated_at) "
                "VALUES (:component_id, :revision, NULL, :certification_tier, "
                ":status, NULL, :content_hash, :id, :created_at, :updated_at)"
            ),
            {
                "component_id": "component-1",
                "revision": "fixture-1",
                "certification_tier": "provisional",
                "status": "draft",
                "content_hash": "legacy-hash",
                "id": "revision-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO component_parameters "
                "(revision_id, field_name, value_si, unit_si, original_value, "
                "original_unit, id, created_at, updated_at) "
                "VALUES (:revision_id, :field_name, :value_si, :unit_si, "
                ":original_value, :original_unit, :id, :created_at, :updated_at)"
            ),
            {
                "revision_id": "revision-1",
                "field_name": "nominal_voltage_v",
                "value_si": "36.0",
                "unit_si": "V",
                "original_value": "36.0",
                "original_unit": "V",
                "id": "parameter-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO source_documents "
                "(source_url, title, document_hash, rights_status, content_type, "
                "id, created_at, updated_at) "
                "VALUES (:source_url, :title, :document_hash, :rights_status, "
                ":content_type, :id, :created_at, :updated_at)"
            ),
            {
                "source_url": "fixture://prometheus/pm-36-gm/fixture-1",
                "title": "Synthetic fixture",
                "document_hash": "legacy-document-hash",
                "rights_status": "project_fixture",
                "content_type": "application/json",
                "id": "document-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO evidence_records "
                "(parameter_id, source_document_id, source_locator, source_excerpt, "
                "evidence_class, confidence, extraction_method, review_status, "
                "reviewed_by, reviewed_at, id, created_at, updated_at) "
                "VALUES (:parameter_id, :source_document_id, :source_locator, "
                ":source_excerpt, :evidence_class, :confidence, :extraction_method, "
                ":review_status, NULL, NULL, :id, :created_at, :updated_at)"
            ),
            {
                "parameter_id": "parameter-1",
                "source_document_id": "document-1",
                "source_locator": "parameters.nominal_voltage_v",
                "source_excerpt": "nominal_voltage_v: 36.0 V",
                "evidence_class": "synthetic_fixture",
                "confidence": "0.99",
                "extraction_method": "fixture_json_v1",
                "review_status": "pending",
                "id": "evidence-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO research_jobs "
                "(idempotency_key, status, query, revision_id, provider, "
                "schema_version, id, created_at, updated_at) "
                "VALUES (:idempotency_key, :status, :query, :revision_id, :provider, "
                ":schema_version, :id, :created_at, :updated_at)"
            ),
            {
                "idempotency_key": "migration-test",
                "status": "ready_for_review",
                "query": "{}",
                "revision_id": "revision-1",
                "provider": "fixture",
                "schema_version": "1.0.0",
                "id": "job-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO research_job_events "
                "(job_id, stage, sequence, message, id, created_at, updated_at) "
                "VALUES (:job_id, :stage, :sequence, :message, :id, :created_at, "
                ":updated_at)"
            ),
            {
                "job_id": "job-1",
                "stage": "identity_resolution",
                "sequence": "1",
                "message": "Identity Resolution",
                "id": "event-1",
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
    migration_engine.dispose()


def decoded_json(value):
    return json.loads(value) if isinstance(value, str) else value


def test_typed_migration_upgrades_and_round_trips_legacy_scalar(tmp_path):
    database_url = f"sqlite:///{tmp_path / 'migration.db'}"
    config = migration_config(database_url)
    command.upgrade(config, BASE_REVISION)
    seed_legacy_component(database_url)

    command.upgrade(config, "head")
    migration_engine = sa.create_engine(database_url)
    inspector = sa.inspect(migration_engine)
    revision_columns = {
        column["name"]: column for column in inspector.get_columns("component_revisions")
    }
    parameter_columns = {
        column["name"]: column for column in inspector.get_columns("component_parameters")
    }
    evidence_columns = {
        column["name"]: column for column in inspector.get_columns("evidence_records")
    }
    event_columns = {
        column["name"]: column for column in inspector.get_columns("research_job_events")
    }
    assert revision_columns["content_hash"]["nullable"] is True
    assert {
        "supported_recipes_json",
        "missing_information_json",
        "limitations_json",
    }.issubset(revision_columns)
    assert "value_si" not in parameter_columns
    assert {
        "quantity",
        "dimension",
        "value_json",
        "validity_conditions_json",
    }.issubset(parameter_columns)
    assert isinstance(evidence_columns["confidence"]["type"], sa.Numeric)
    assert "review_note" in evidence_columns
    assert isinstance(event_columns["sequence"]["type"], sa.Integer)
    with migration_engine.connect() as connection:
        row = connection.execute(
            sa.text(
                "SELECT value_json, quantity, dimension, validity_conditions_json "
                "FROM component_parameters WHERE id='parameter-1'"
            )
        ).mappings().one()
        confidence = connection.execute(
            sa.text("SELECT confidence FROM evidence_records WHERE id='evidence-1'")
        ).scalar_one()
        sequence = connection.execute(
            sa.text("SELECT sequence FROM research_job_events WHERE id='event-1'")
        ).scalar_one()
    assert decoded_json(row["value_json"]) == {"kind": "scalar", "value": 36.0}
    assert row["quantity"] == "voltage"
    assert row["dimension"] == "electric_potential"
    assert decoded_json(row["validity_conditions_json"]) == [
        "synthetic conformance fixture only"
    ]
    assert Decimal(str(confidence)) == Decimal("0.99")
    assert sequence == 1
    migration_engine.dispose()

    command.downgrade(config, BASE_REVISION)
    migration_engine = sa.create_engine(database_url)
    with migration_engine.connect() as connection:
        legacy = connection.execute(
            sa.text("SELECT value_si FROM component_parameters WHERE id='parameter-1'")
        ).scalar_one()
    assert legacy == "36.0"
    migration_engine.dispose()


def test_typed_migration_rejects_lossy_downgrade(tmp_path):
    database_url = f"sqlite:///{tmp_path / 'lossy.db'}"
    config = migration_config(database_url)
    command.upgrade(config, BASE_REVISION)
    seed_legacy_component(database_url)
    command.upgrade(config, "head")
    migration_engine = sa.create_engine(database_url)
    with migration_engine.begin() as connection:
        connection.execute(
            sa.text(
                "UPDATE component_parameters SET value_json=:value WHERE id='parameter-1'"
            ),
            {"value": json.dumps({"kind": "range", "minimum": 30, "maximum": 40})},
        )
    migration_engine.dispose()
    with pytest.raises(RuntimeError, match="cannot downgrade non-scalar engineering value"):
        command.downgrade(config, BASE_REVISION)


def test_database_foreign_keys_are_enforced():
    if engine.dialect.name == "sqlite":
        with engine.connect() as connection:
            assert connection.exec_driver_sql("PRAGMA foreign_keys").scalar_one() == 1
    with SessionLocal() as db:
        db.add(
            Component(
                manufacturer_id="missing-manufacturer",
                part_number="INVALID",
                normalized_part_number="invalid",
                family="",
                model_class="unknown",
            )
        )
        with pytest.raises(IntegrityError):
            db.commit()


def test_record_updated_at_advances_on_mutation():
    with SessionLocal() as db:
        project = Project(name="Timestamp check")
        db.add(project)
        db.commit()
        original = project.updated_at
        project.description = "changed"
        db.commit()
        assert project.updated_at != original
