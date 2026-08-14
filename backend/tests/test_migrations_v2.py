from __future__ import annotations

from datetime import datetime, timezone
import json
import os
from pathlib import Path
from uuid import uuid4

import pytest
import sqlalchemy as sa
from alembic import command
from alembic.config import Config
from alembic.migration import MigrationContext

from app.contracts_v2 import SCHEMA_ID, SCHEMA_VERSION
from app.db_types import UtcTimestamp


BACKEND_ROOT = Path(__file__).parents[1]
PREVIOUS_HEAD = "7b6d91e2a4f0"
HEAD_REVISION = "a41f0c93e2d7"
STAMP = "2026-08-11T00:00:00.000000Z"
LEGACY_STAMP = "2026-08-11T00:00:00+00:00"
HASH_A = "sha256:" + "a" * 64
HASH_B = "sha256:" + "b" * 64
REVISION_ID = "10000000-0000-4000-8000-000000000001"
SECOND_REVISION_ID = "10000000-0000-4000-8000-000000000002"
SLOT_ID = "30000000-0000-4000-8000-000000000001"
CLAIM_ID = "40000000-0000-4000-8000-000000000001"
SECOND_CLAIM_ID = "40000000-0000-4000-8000-000000000002"
EVIDENCE_ID = "50000000-0000-4000-8000-000000000001"
SECOND_EVIDENCE_ID = "50000000-0000-4000-8000-000000000002"
REVIEW_ID = "60000000-0000-4000-8000-000000000001"
GATE_ID = "70000000-0000-4000-8000-000000000001"
DATABASE_TARGETS = ["sqlite"] + (
    ["postgresql"] if os.getenv("PROMETHEUS_TEST_POSTGRES_URL") else []
)


def migration_config(database_url: str) -> Config:
    config = Config(str(BACKEND_ROOT / "alembic.ini"))
    config.set_main_option("script_location", str(BACKEND_ROOT / "migrations"))
    config.attributes["database_url"] = database_url.replace("%", "%%")
    return config


def sqlite_engine(database_url: str) -> sa.Engine:
    engine = sa.create_engine(database_url)

    @sa.event.listens_for(engine, "connect")
    def enable_foreign_keys(dbapi_connection, _connection_record):
        cursor = dbapi_connection.cursor()
        cursor.execute("PRAGMA foreign_keys=ON")
        cursor.close()

    return engine


@pytest.fixture(params=DATABASE_TARGETS)
def v2_engine(request, tmp_path):
    if request.param == "sqlite":
        database_url = f"sqlite:///{tmp_path / 'v2.db'}"
        command.upgrade(migration_config(database_url), "head")
        engine = sqlite_engine(database_url)
        yield engine
        engine.dispose()
        return

    base_url = sa.engine.make_url(os.environ["PROMETHEUS_TEST_POSTGRES_URL"])
    if base_url.drivername == "postgresql":
        base_url = base_url.set(drivername="postgresql+psycopg")
    schema_name = f"prometheus_test_{uuid4().hex}"
    administrative_engine = sa.create_engine(base_url)
    with administrative_engine.begin() as connection:
        connection.execute(sa.schema.CreateSchema(schema_name))
    isolated_url = base_url.update_query_dict(
        {"options": f"-csearch_path={schema_name}"}
    )
    engine = None
    try:
        command.upgrade(
            migration_config(isolated_url.render_as_string(hide_password=False)),
            "head",
        )
        engine = sa.create_engine(isolated_url)
        yield engine
    finally:
        if engine is not None:
            engine.dispose()
        with administrative_engine.begin() as connection:
            connection.execute(
                sa.schema.DropSchema(schema_name, cascade=True, if_exists=True)
            )
        administrative_engine.dispose()


def _insert_component_revision(
    connection: sa.Connection,
    *,
    revision_id: str = REVISION_ID,
    suffix: str = "1",
) -> None:
    manufacturer_id = f"manufacturer-{suffix}"
    component_id = f"component-{suffix}"
    connection.execute(
        sa.text(
            "INSERT INTO manufacturers "
            "(name, normalized_name, id, created_at, updated_at) "
            "VALUES (:name, :normalized, :id, :created_at, :updated_at)"
        ),
        {
            "name": f"V2 Fixture Works {suffix}",
            "normalized": f"v2fixtureworks{suffix}",
            "id": manufacturer_id,
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
            "manufacturer_id": manufacturer_id,
            "part_number": f"PM-V2-{suffix}",
            "normalized_part_number": f"pmv2{suffix}",
            "family": "synthetic",
            "model_class": "gearmotor",
            "id": component_id,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )
    connection.execute(
        sa.text(
            "INSERT INTO component_revisions "
            "(component_id, revision, parent_revision_id, certification_tier, "
            "status, published_at, content_hash, supported_recipes_json, "
            "missing_information_json, limitations_json, draft_version, "
            "contract_schema_id, contract_schema_version, publication_integrity, "
            "published_object_hash, id, created_at, updated_at) "
            "VALUES (:component_id, :revision, NULL, 'provisional', 'draft', "
            "NULL, NULL, :empty, :empty, :empty, 0, :schema_id, :schema_version, "
            "'v2_draft', NULL, :id, :created_at, :updated_at)"
        ),
        {
            "component_id": component_id,
            "revision": f"fixture-v2-{suffix}",
            "empty": json.dumps([]),
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
            "id": revision_id,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )


def _insert_artifact(connection: sa.Connection, object_hash: str = HASH_A) -> None:
    connection.execute(
        sa.text(
            "INSERT INTO artifact_objects_v2 "
            "(object_hash, payload_bytes, byte_length, media_type, origin_path, "
            "original_filename, created_at) "
            "VALUES (:hash, :payload, :length, 'application/json', "
            "'/fixture/source.json', 'source.json', :created_at)"
        ),
        {
            "hash": object_hash,
            "payload": b"{}",
            "length": 2,
            "created_at": STAMP,
        },
    )


def _insert_slot(
    connection: sa.Connection,
    *,
    revision_id: str = REVISION_ID,
    slot_id: str = SLOT_ID,
    name: str = "nominal_voltage_v",
) -> None:
    connection.execute(
        sa.text(
            "INSERT INTO parameter_slots_v2 "
            "(id, revision_id, name, quantity, dimension, "
            "required_for_execution, created_at, updated_at) "
            "VALUES (:id, :revision_id, :name, 'voltage', "
            "'electric_potential', TRUE, :created_at, :updated_at)"
        ),
        {
            "id": slot_id,
            "revision_id": revision_id,
            "name": name,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )


def _insert_claim(
    connection: sa.Connection,
    *,
    claim_id: str = CLAIM_ID,
    revision_id: str = REVISION_ID,
    slot_id: str = SLOT_ID,
    finalized: bool = False,
    fingerprint: str | None = None,
) -> None:
    requested_finalization = finalized
    connection.execute(
        sa.text(
            "INSERT INTO candidate_claims_v2 "
            "(id, revision_id, slot_id, value_state, value_json, unit, "
            "original_value, original_unit, reason, validity_conditions_json, "
            "provenance, finalized, fingerprint, created_at, updated_at) "
            "VALUES (:id, :revision_id, :slot_id, 'known', :value, 'V', "
            "'36.0', 'V', NULL, :conditions, 'fixture_json_v2', FALSE, "
            "NULL, :created_at, :updated_at)"
        ),
        {
            "id": claim_id,
            "revision_id": revision_id,
            "slot_id": slot_id,
            "value": json.dumps({"kind": "scalar", "value": 36.0}),
            "conditions": json.dumps(["synthetic fixture"]),
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )
    if requested_finalization:
        connection.execute(
            sa.text(
                "UPDATE candidate_claims_v2 SET finalized=TRUE, fingerprint=:fingerprint, "
                "updated_at=:updated_at WHERE id=:id"
            ),
            {"fingerprint": fingerprint, "updated_at": STAMP, "id": claim_id},
        )


def _insert_evidence(
    connection: sa.Connection,
    *,
    evidence_id: str = EVIDENCE_ID,
    revision_id: str = REVISION_ID,
    confidence: float | None = None,
    artifact_hash: str = HASH_A,
) -> None:
    connection.execute(
        sa.text(
            "INSERT INTO evidence_records_v2 "
            "(id, revision_id, evidence_class, source_authority, "
            "physical_validation_status, extraction_confidence, artifact_hash, "
            "local_provenance, limitations_json, created_at) "
            "VALUES (:id, :revision_id, 'private_upload', 'synthetic_fixture', "
            "'unvalidated', :confidence, :artifact_hash, 'test fixture', "
            ":limitations, :created_at)"
        ),
        {
            "id": evidence_id,
            "revision_id": revision_id,
            "confidence": confidence,
            "artifact_hash": artifact_hash,
            "limitations": json.dumps(["test only"]),
            "created_at": STAMP,
        },
    )


def _seed_claim_graph(
    connection: sa.Connection, *, finalized: bool = False
) -> None:
    _insert_component_revision(connection)
    _insert_artifact(connection)
    _insert_slot(connection)
    _insert_claim(
        connection,
        finalized=finalized,
        fingerprint=HASH_B if finalized else None,
    )
    _insert_evidence(connection)
    if not finalized:
        connection.execute(
            sa.text(
                "INSERT INTO claim_evidence_links_v2 "
                "(revision_id, claim_id, evidence_id, created_at) "
                "VALUES (:revision_id, :claim_id, :evidence_id, :created_at)"
            ),
            {
                "revision_id": REVISION_ID,
                "claim_id": CLAIM_ID,
                "evidence_id": EVIDENCE_ID,
                "created_at": STAMP,
            },
        )


def _assert_integrity_error(
    engine: sa.Engine, statement: str, parameters: dict[str, object]
) -> None:
    with engine.connect() as connection:
        transaction = connection.begin()
        # SQLite reports CHECK/trigger failures as IntegrityError, while
        # PostgreSQL classifies RAISE EXCEPTION from integrity triggers as a
        # ProgrammingError.  Both are DBAPIError subclasses and both mean the
        # database—not application code—rejected the invalid state.
        with pytest.raises(sa.exc.DBAPIError):
            connection.execute(sa.text(statement), parameters)
        transaction.rollback()


def test_migration_head_and_v2_table_inventory(v2_engine):
    expected_tables = {
        "artifact_objects_v2",
        "parameter_slots_v2",
        "candidate_claims_v2",
        "evidence_records_v2",
        "claim_evidence_links_v2",
        "evidence_parent_claims_v2",
        "evidence_parent_evidence_v2",
        "claim_selections_v2",
        "claim_review_events_v2",
        "capability_gates_v2",
        "published_objects",
        "fixture_ingestion_jobs_v2",
        "publication_requests",
    }
    assert expected_tables.issubset(sa.inspect(v2_engine).get_table_names())
    with v2_engine.connect() as connection:
        assert MigrationContext.configure(connection).get_current_revision() == HEAD_REVISION
        if v2_engine.dialect.name == "postgresql":
            server_version_num = int(
                connection.exec_driver_sql("SHOW server_version_num").scalar_one()
            )
            assert server_version_num // 10_000 == 17


def test_database_rejects_non_uuid_domain_ids_and_non_hex_hashes(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO parameter_slots_v2 "
        "(id, revision_id, name, quantity, dimension, required_for_execution, "
        "created_at, updated_at) VALUES ('not-a-uuid', :revision, 'voltage', "
        "'voltage', 'electric_potential', TRUE, :created_at, :updated_at)",
        {"revision": REVISION_ID, "created_at": STAMP, "updated_at": STAMP},
    )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO artifact_objects_v2 "
        "(object_hash, payload_bytes, byte_length, media_type, origin_path, "
        "original_filename, created_at) VALUES (:hash, :payload, 2, "
        "'application/json', '/fixture', 'fixture.json', :created_at)",
        {"hash": "sha256:" + "g" * 64, "payload": b"{}", "created_at": STAMP},
    )


def test_candidate_claim_must_begin_unfinalized(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_slot(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO candidate_claims_v2 "
        "(id, revision_id, slot_id, value_state, value_json, unit, "
        "original_value, original_unit, reason, validity_conditions_json, "
        "provenance, finalized, fingerprint, created_at, updated_at) VALUES "
        "(:id, :revision, :slot, 'known', :value, 'V', '36', 'V', NULL, "
        ":empty, 'test', TRUE, :fingerprint, :created_at, :updated_at)",
        {
            "id": CLAIM_ID,
            "revision": REVISION_ID,
            "slot": SLOT_ID,
            "value": json.dumps({"kind": "scalar", "value": 36}),
            "empty": json.dumps([]),
            "fingerprint": HASH_B,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )


def test_v2_revision_cannot_shed_or_corrupt_its_contract_identity(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
    for statement in (
        "UPDATE component_revisions SET contract_schema_id=NULL, "
        "contract_schema_version=NULL, publication_integrity=NULL, "
        "draft_version=NULL WHERE id=:id",
        "UPDATE component_revisions SET publication_integrity='invented_state' "
        "WHERE id=:id",
        "UPDATE component_revisions SET contract_schema_version='9.0.0' "
        "WHERE id=:id",
    ):
        _assert_integrity_error(v2_engine, statement, {"id": REVISION_ID})


def _seed_legacy_publication(
    engine: sa.Engine, *, published_at: str = LEGACY_STAMP
) -> None:
    with engine.begin() as connection:
        connection.execute(
            sa.text(
                "INSERT INTO manufacturers "
                "(name, normalized_name, id, created_at, updated_at) "
                "VALUES ('Legacy Works', 'legacyworks', 'legacy-manufacturer', "
                ":stamp, :stamp)"
            ),
            {"stamp": LEGACY_STAMP},
        )
        connection.execute(
            sa.text(
                "INSERT INTO components "
                "(manufacturer_id, part_number, normalized_part_number, family, "
                "model_class, id, created_at, updated_at) VALUES "
                "('legacy-manufacturer', 'LEG-1', 'leg1', '', 'legacy', "
                "'legacy-component', :stamp, :stamp)"
            ),
            {"stamp": LEGACY_STAMP},
        )
        connection.execute(
            sa.text(
                "INSERT INTO component_revisions "
                "(component_id, revision, parent_revision_id, certification_tier, "
                "status, published_at, content_hash, supported_recipes_json, "
                "missing_information_json, limitations_json, id, created_at, "
                "updated_at) VALUES ('legacy-component', 'r1', NULL, "
                "'provisional', 'published', :published_at, 'legacy-hash', "
                ":empty, :empty, :empty, 'revision-1', :stamp, :stamp)"
            ),
            {
                "published_at": published_at,
                "empty": json.dumps([]),
                "stamp": LEGACY_STAMP,
            },
        )


def test_upgrade_preserves_and_classifies_legacy_publication(tmp_path):
    database_url = f"sqlite:///{tmp_path / 'legacy.db'}"
    config = migration_config(database_url)
    command.upgrade(config, PREVIOUS_HEAD)
    engine = sqlite_engine(database_url)
    _seed_legacy_publication(engine)
    engine.dispose()

    command.upgrade(config, "head")
    engine = sqlite_engine(database_url)
    with engine.connect() as connection:
        row = connection.execute(
            sa.text(
                "SELECT content_hash, published_object_hash, "
                "publication_integrity, published_at FROM component_revisions "
                "WHERE id='revision-1'"
            )
        ).mappings().one()
    assert row["content_hash"] == "legacy-hash"
    assert row["published_object_hash"] is None
    assert row["publication_integrity"] == "legacy_unsealed"
    assert row["published_at"].endswith("Z")
    engine.dispose()


@pytest.mark.parametrize(
    ("published_at", "message"),
    [
        ("2026-08-11T00:00:00", "naive publication timestamp"),
        ("not-a-timestamp", "invalid publication timestamp"),
    ],
)
def test_upgrade_rejects_ambiguous_legacy_publication_timestamp(
    tmp_path, published_at, message
):
    database_url = f"sqlite:///{tmp_path / (message.split()[0] + '.db')}"
    config = migration_config(database_url)
    command.upgrade(config, PREVIOUS_HEAD)
    engine = sqlite_engine(database_url)
    _seed_legacy_publication(engine, published_at=published_at)
    engine.dispose()

    with pytest.raises(RuntimeError, match=message):
        command.upgrade(config, "head")


def test_utc_timestamp_round_trip_and_naive_rejection():
    metadata = sa.MetaData()
    table = sa.Table(
        "utc_values",
        metadata,
        sa.Column("id", sa.Integer, primary_key=True),
        sa.Column("observed_at", UtcTimestamp(), nullable=False),
    )
    engine = sa.create_engine("sqlite://")
    metadata.create_all(engine)
    instant = datetime(2026, 8, 11, 12, 30, tzinfo=timezone.utc)
    with engine.begin() as connection:
        connection.execute(table.insert().values(id=1, observed_at=instant))
    with engine.connect() as connection:
        loaded = connection.execute(sa.select(table.c.observed_at)).scalar_one()
        raw = connection.exec_driver_sql(
            "SELECT observed_at FROM utc_values WHERE id=1"
        ).scalar_one()
    assert loaded == instant
    assert loaded.tzinfo is not None
    assert raw.endswith("Z")
    with pytest.raises(sa.exc.StatementError):
        with engine.begin() as connection:
            connection.execute(
                table.insert().values(id=2, observed_at=datetime(2026, 8, 11))
            )
    engine.dispose()


def test_direct_invalid_confidence_is_rejected(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_artifact(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO evidence_records_v2 "
        "(id, revision_id, evidence_class, source_authority, "
        "physical_validation_status, extraction_confidence, artifact_hash, "
        "local_provenance, limitations_json, created_at) "
        "VALUES (:id, :revision, 'private_upload', 'synthetic_fixture', "
        "'unvalidated', 1.01, :artifact, 'fixture', :limitations, :created_at)",
        {
            "id": EVIDENCE_ID,
            "revision": REVISION_ID,
            "artifact": HASH_A,
            "limitations": json.dumps([]),
            "created_at": STAMP,
        },
    )


@pytest.mark.parametrize(
    ("column", "value"),
    [
        ("evidence_class", "solver_output"),
        ("source_authority", "internet_guess"),
        ("physical_validation_status", "probably_valid"),
    ],
)
def test_evidence_closed_vocabularies_are_database_constraints(
    v2_engine, column, value
):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_artifact(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO evidence_records_v2 "
        "(id, revision_id, evidence_class, source_authority, "
        "physical_validation_status, extraction_confidence, artifact_hash, "
        "local_provenance, limitations_json, created_at) "
        f"VALUES (:id, :revision, :evidence_class, :source_authority, "
        ":physical_validation_status, NULL, :artifact, 'fixture', :limitations, "
        ":created_at)",
        {
            "id": EVIDENCE_ID,
            "revision": REVISION_ID,
            "artifact": HASH_A,
            "evidence_class": value if column == "evidence_class" else "private_upload",
            "source_authority": value if column == "source_authority" else "synthetic_fixture",
            "physical_validation_status": (
                value if column == "physical_validation_status" else "unvalidated"
            ),
            "limitations": json.dumps([]),
            "created_at": STAMP,
        },
    )


@pytest.mark.parametrize(
    "shape",
    [
        {
            "value_state": "known",
            "value": None,
            "unit": "V",
            "original_value": "36",
            "original_unit": "V",
            "reason": None,
        },
        {
            "value_state": "known",
            "value": json.dumps({"kind": "scalar", "value": 36}),
            "unit": "V",
            "original_value": "36",
            "original_unit": "V",
            "reason": "conflicting reason",
        },
        {
            "value_state": "unknown",
            "value": json.dumps({"kind": "unknown", "reason": "missing"}),
            "unit": None,
            "original_value": None,
            "original_unit": None,
            "reason": "missing",
        },
        {
            "value_state": "unsupported",
            "value": None,
            "unit": None,
            "original_value": None,
            "original_unit": None,
            "reason": "missing",
        },
    ],
)
def test_candidate_known_unknown_shape_is_enforced(v2_engine, shape):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_slot(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO candidate_claims_v2 "
        "(id, revision_id, slot_id, value_state, value_json, unit, "
        "original_value, original_unit, reason, validity_conditions_json, "
        "provenance, finalized, fingerprint, created_at, updated_at) "
        "VALUES (:id, :revision, :slot, :value_state, :value, :unit, "
        ":original_value, :original_unit, :reason, :conditions, 'test', 0, "
        "NULL, :created_at, :updated_at)",
        {
            "id": CLAIM_ID,
            "revision": REVISION_ID,
            "slot": SLOT_ID,
            "conditions": json.dumps([]),
            "created_at": STAMP,
            "updated_at": STAMP,
            **shape,
        },
    )


def test_slot_selection_and_same_revision_constraints(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_component_revision(
            connection, revision_id=SECOND_REVISION_ID, suffix="2"
        )
        _insert_slot(connection)
        _insert_slot(
            connection,
            revision_id=SECOND_REVISION_ID,
            slot_id="30000000-0000-4000-8000-000000000002",
            name="nominal_voltage_v",
        )
        _insert_claim(connection, finalized=True, fingerprint=HASH_B)

    _assert_integrity_error(
        v2_engine,
        "INSERT INTO parameter_slots_v2 "
        "(id, revision_id, name, quantity, dimension, required_for_execution, "
        "created_at, updated_at) VALUES (:id, :revision, 'nominal_voltage_v', "
        "'voltage', 'electric_potential', TRUE, :created_at, :updated_at)",
        {
            "id": "30000000-0000-4000-8000-000000000003",
            "revision": REVISION_ID,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO candidate_claims_v2 "
        "(id, revision_id, slot_id, value_state, value_json, unit, "
        "original_value, original_unit, reason, validity_conditions_json, "
        "provenance, finalized, fingerprint, created_at, updated_at) "
        "VALUES (:id, :revision, :slot, 'known', :value, 'V', '36', 'V', "
        "NULL, :empty, 'test', FALSE, NULL, :created_at, :updated_at)",
        {
            "id": SECOND_CLAIM_ID,
            "revision": SECOND_REVISION_ID,
            "slot": SLOT_ID,
            "value": json.dumps({"kind": "scalar", "value": 36}),
            "empty": json.dumps([]),
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )
    with v2_engine.begin() as connection:
        connection.execute(
            sa.text(
                "INSERT INTO claim_selections_v2 "
                "(id, revision_id, slot_id, claim_id, created_at, updated_at) "
                "VALUES (:id, :revision, :slot, :claim, :created_at, :updated_at)"
            ),
            {
                "id": "31000000-0000-4000-8000-000000000001",
                "revision": REVISION_ID,
                "slot": SLOT_ID,
                "claim": CLAIM_ID,
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_selections_v2 "
        "(id, revision_id, slot_id, claim_id, created_at, updated_at) "
        "VALUES (:id, :revision, :slot, :claim, :created_at, :updated_at)",
        {
            "id": "31000000-0000-4000-8000-000000000002",
            "revision": REVISION_ID,
            "slot": SLOT_ID,
            "claim": CLAIM_ID,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )


def test_claim_finalization_and_link_lifecycle(v2_engine):
    with v2_engine.begin() as connection:
        _seed_claim_graph(connection)
        connection.execute(
            sa.text(
                "UPDATE candidate_claims_v2 SET finalized=TRUE, fingerprint=:hash, "
                "updated_at=:updated_at WHERE id=:id"
            ),
            {"hash": HASH_B, "updated_at": STAMP, "id": CLAIM_ID},
        )

    _assert_integrity_error(
        v2_engine,
        "UPDATE candidate_claims_v2 SET fingerprint=:hash WHERE id=:id",
        {"hash": HASH_A, "id": CLAIM_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "UPDATE candidate_claims_v2 SET unit='mV' WHERE id=:id",
        {"id": CLAIM_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "DELETE FROM candidate_claims_v2 WHERE id=:id",
        {"id": CLAIM_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_evidence_links_v2 "
        "(revision_id, claim_id, evidence_id, created_at) "
        "VALUES (:revision, :claim, :evidence, :created_at)",
        {
            "revision": REVISION_ID,
            "claim": CLAIM_ID,
            "evidence": EVIDENCE_ID,
            "created_at": STAMP,
        },
    )
    _assert_integrity_error(
        v2_engine,
        "DELETE FROM claim_evidence_links_v2 WHERE claim_id=:claim",
        {"claim": CLAIM_ID},
    )


def test_selection_and_review_require_finalized_claim(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_slot(connection)
        _insert_claim(connection)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_selections_v2 "
        "(id, revision_id, slot_id, claim_id, created_at, updated_at) "
        "VALUES (:id, :revision, :slot, :claim, :created_at, :updated_at)",
        {
            "id": "31000000-0000-4000-8000-000000000001",
            "revision": REVISION_ID,
            "slot": SLOT_ID,
            "claim": CLAIM_ID,
            "created_at": STAMP,
            "updated_at": STAMP,
        },
    )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_review_events_v2 "
        "(id, revision_id, claim_id, claim_fingerprint, decision, reviewed_by, "
        "note, reviewed_at, applied_draft_version, created_at) "
        "VALUES (:id, :revision, :claim, :fingerprint, 'accepted', 'reviewer', "
        "'checked', :reviewed_at, 1, :created_at)",
        {
            "id": REVIEW_ID,
            "revision": REVISION_ID,
            "claim": CLAIM_ID,
            "fingerprint": HASH_B,
            "reviewed_at": STAMP,
            "created_at": STAMP,
        },
    )


def test_review_fingerprint_and_append_only_rules(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_slot(connection)
        _insert_claim(connection, finalized=True, fingerprint=HASH_B)
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_review_events_v2 "
        "(id, revision_id, claim_id, claim_fingerprint, decision, reviewed_by, "
        "note, reviewed_at, applied_draft_version, created_at) "
        "VALUES (:id, :revision, :claim, :fingerprint, 'accepted', 'reviewer', "
        "'checked', :reviewed_at, 1, :created_at)",
        {
            "id": REVIEW_ID,
            "revision": REVISION_ID,
            "claim": CLAIM_ID,
            "fingerprint": HASH_A,
            "reviewed_at": STAMP,
            "created_at": STAMP,
        },
    )
    with v2_engine.begin() as connection:
        connection.execute(
            sa.text(
                "INSERT INTO claim_review_events_v2 "
                "(id, revision_id, claim_id, claim_fingerprint, decision, "
                "reviewed_by, note, reviewed_at, applied_draft_version, created_at) "
                "VALUES (:id, :revision, :claim, :fingerprint, 'accepted', "
                "'reviewer', 'checked', :reviewed_at, 1, :created_at)"
            ),
            {
                "id": REVIEW_ID,
                "revision": REVISION_ID,
                "claim": CLAIM_ID,
                "fingerprint": HASH_B,
                "reviewed_at": STAMP,
                "created_at": STAMP,
            },
        )
    _assert_integrity_error(
        v2_engine,
        "UPDATE claim_review_events_v2 SET note='changed' WHERE id=:id",
        {"id": REVIEW_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "DELETE FROM claim_review_events_v2 WHERE id=:id",
        {"id": REVIEW_ID},
    )


def test_artifact_evidence_and_published_objects_are_immutable(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_artifact(connection)
        _insert_evidence(connection)
        connection.execute(
            sa.text(
                "INSERT INTO published_objects "
                "(object_hash, payload_bytes, byte_length, media_type, schema_id, "
                "schema_version, canonicalization, created_at) VALUES "
                "(:hash, :payload, :length, :media_type, :schema_id, "
                ":schema_version, 'RFC8785', :created_at)"
            ),
            {
                "hash": HASH_B,
                "payload": b"{}",
                "length": 2,
                "media_type": (
                    "application/vnd.prometheus.execution-component+json;"
                    "version=2.0.0"
                ),
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "created_at": STAMP,
            },
        )
    for statement, parameters in (
        (
            "UPDATE artifact_objects_v2 SET byte_length=3 WHERE object_hash=:id",
            {"id": HASH_A},
        ),
        (
            "DELETE FROM artifact_objects_v2 WHERE object_hash=:id",
            {"id": HASH_A},
        ),
        (
            "UPDATE evidence_records_v2 SET local_provenance='changed' WHERE id=:id",
            {"id": EVIDENCE_ID},
        ),
        (
            "DELETE FROM evidence_records_v2 WHERE id=:id",
            {"id": EVIDENCE_ID},
        ),
        (
            "UPDATE published_objects SET byte_length=3 WHERE object_hash=:id",
            {"id": HASH_B},
        ),
        (
            "DELETE FROM published_objects WHERE object_hash=:id",
            {"id": HASH_B},
        ),
    ):
        _assert_integrity_error(v2_engine, statement, parameters)


def test_sealed_revision_requires_exact_object_binding_and_is_immutable(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        connection.execute(
            sa.text(
                "INSERT INTO published_objects "
                "(object_hash, payload_bytes, byte_length, media_type, schema_id, "
                "schema_version, canonicalization, created_at) VALUES "
                "(:hash, :payload, 2, 'application/json', :schema_id, "
                ":schema_version, 'RFC8785', :created_at)"
            ),
            {
                "hash": HASH_B,
                "payload": b"{}",
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "created_at": STAMP,
            },
        )
    _assert_integrity_error(
        v2_engine,
        "UPDATE component_revisions SET status='published', "
        "publication_integrity='sealed_v2', content_hash=:content_hash, "
        "published_object_hash=:object_hash, published_at=NULL WHERE id=:id",
        {"content_hash": HASH_B, "object_hash": HASH_B, "id": REVISION_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "UPDATE component_revisions SET status='published', "
        "publication_integrity='sealed_v2', content_hash=:content_hash, "
        "published_object_hash=:object_hash, published_at=:published_at WHERE id=:id",
        {
            "content_hash": HASH_A,
            "object_hash": HASH_B,
            "published_at": STAMP,
            "id": REVISION_ID,
        },
    )
    with v2_engine.begin() as connection:
        connection.execute(
            sa.text(
                "UPDATE component_revisions SET status='published', "
                "publication_integrity='sealed_v2', content_hash=:hash, "
                "published_object_hash=:hash, published_at=:published_at "
                "WHERE id=:id"
            ),
            {
                "hash": HASH_B,
                "published_at": STAMP,
                "id": REVISION_ID,
            },
        )
    _assert_integrity_error(
        v2_engine,
        "UPDATE component_revisions SET draft_version=1 WHERE id=:id",
        {"id": REVISION_ID},
    )
    _assert_integrity_error(
        v2_engine,
        "DELETE FROM component_revisions WHERE id=:id",
        {"id": REVISION_ID},
    )


def test_sealed_revision_rejects_child_graph_mutation(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_artifact(connection)
        _insert_slot(connection)
        _insert_claim(connection, finalized=True, fingerprint=HASH_B)
        _insert_evidence(connection)
        connection.execute(
            sa.text(
                "INSERT INTO claim_selections_v2 "
                "(id, revision_id, slot_id, claim_id, created_at, updated_at) "
                "VALUES (:id, :revision, :slot, :claim, :created_at, :updated_at)"
            ),
            {
                "id": "31000000-0000-4000-8000-000000000001",
                "revision": REVISION_ID,
                "slot": SLOT_ID,
                "claim": CLAIM_ID,
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO capability_gates_v2 "
                "(id, revision_id, capability_id, phase, required_review_type, "
                "state, satisfying_references_json, reason, created_at, updated_at) "
                "VALUES (:id, :revision, 'component_input', 'publication', "
                "'claim_review', 'satisfied', :references, NULL, :created_at, "
                ":updated_at)"
            ),
            {
                "id": GATE_ID,
                "revision": REVISION_ID,
                "references": json.dumps([CLAIM_ID]),
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO published_objects "
                "(object_hash, payload_bytes, byte_length, media_type, schema_id, "
                "schema_version, canonicalization, created_at) VALUES "
                "(:hash, :payload, 2, 'application/json', :schema_id, "
                ":schema_version, 'RFC8785', :created_at)"
            ),
            {
                "hash": HASH_B,
                "payload": b"{}",
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "created_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "UPDATE component_revisions SET status='published', "
                "publication_integrity='sealed_v2', content_hash=:hash, "
                "published_object_hash=:hash, published_at=:published_at "
                "WHERE id=:id"
            ),
            {
                "hash": HASH_B,
                "published_at": STAMP,
                "id": REVISION_ID,
            },
        )

    mutations = [
        (
            "INSERT INTO parameter_slots_v2 "
            "(id, revision_id, name, quantity, dimension, required_for_execution, "
            "created_at, updated_at) VALUES (:child_id, :revision, 'another_slot', "
            "'voltage', 'electric_potential', TRUE, :created_at, :updated_at)",
            {"child_id": "30000000-0000-4000-8000-000000000002"},
        ),
        (
            "UPDATE parameter_slots_v2 SET name='changed' WHERE id=:slot",
            {"slot": SLOT_ID},
        ),
        (
            "UPDATE claim_selections_v2 SET updated_at=:updated_at WHERE "
            "revision_id=:revision",
            {},
        ),
        (
            "INSERT INTO claim_review_events_v2 "
            "(id, revision_id, claim_id, claim_fingerprint, decision, reviewed_by, "
            "note, reviewed_at, applied_draft_version, created_at) VALUES "
            "(:review, :revision, :claim, :fingerprint, 'accepted', 'reviewer', "
            "'late review', :reviewed_at, 1, :created_at)",
            {
                "review": REVIEW_ID,
                "claim": CLAIM_ID,
                "fingerprint": HASH_B,
                "reviewed_at": STAMP,
            },
        ),
        (
            "UPDATE capability_gates_v2 SET state='blocked', reason='changed', "
            "satisfying_references_json=:empty WHERE id=:gate",
            {"empty": json.dumps([]), "gate": GATE_ID},
        ),
    ]
    for statement, specific in mutations:
        _assert_integrity_error(
            v2_engine,
            statement,
            {
                **specific,
                "revision": REVISION_ID,
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )


def test_gate_ingestion_and_publication_states_are_closed(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
    invalid_statements = [
        (
            "INSERT INTO capability_gates_v2 "
            "(id, revision_id, capability_id, phase, required_review_type, state, "
            "satisfying_references_json, reason, created_at, updated_at) "
            "VALUES (:id, :revision, 'component_input', 'build', "
            "'claim_review', 'pending', :empty, 'waiting', :created_at, :updated_at)",
            {"id": GATE_ID, "revision": REVISION_ID},
        ),
        (
            "INSERT INTO fixture_ingestion_jobs_v2 "
            "(id, fixture_id, idempotency_key, status, revision_id, artifact_hash, "
            "error_code, error_message, created_at, updated_at) VALUES "
            "(:id, 'fixture', 'fixture-key', 'maybe', NULL, NULL, NULL, NULL, "
            ":created_at, :updated_at)",
            {"id": "71000000-0000-4000-8000-000000000001"},
        ),
        (
            "INSERT INTO publication_requests "
            "(id, operation, idempotency_key, revision_id, request_fingerprint, "
            "state, response_status, response_body, response_headers_json, "
            "published_object_hash, created_at, updated_at) VALUES "
            "(:id, 'publication', 'publication-key-01', :revision, :fingerprint, "
            "'maybe', NULL, NULL, NULL, NULL, :created_at, :updated_at)",
            {
                "id": "72000000-0000-4000-8000-000000000001",
                "revision": REVISION_ID,
                "fingerprint": HASH_A,
            },
        ),
    ]
    for statement, specific in invalid_statements:
        _assert_integrity_error(
            v2_engine,
            statement,
            {
                **specific,
                "empty": json.dumps([]),
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )


@pytest.mark.parametrize(
    ("status", "has_result", "has_error"),
    [
        ("queued", False, False),
        ("running", False, False),
        ("succeeded", True, False),
        ("failed", False, True),
        ("cancelled", False, True),
        ("timed_out", False, True),
    ],
)
def test_ingestion_job_accepts_exact_contract_states(
    v2_engine, status, has_result, has_error
):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_artifact(connection)
        connection.execute(
            sa.text(
                "INSERT INTO fixture_ingestion_jobs_v2 "
                "(id, fixture_id, idempotency_key, status, revision_id, "
                "artifact_hash, error_code, error_message, created_at, updated_at) "
                "VALUES (:id, 'fixture', :key, :status, :revision, :artifact, "
                ":error_code, :error_message, :created_at, :updated_at)"
            ),
            {
                "id": f"71000000-0000-4000-8000-{len(status):012d}",
                "key": f"fixture-key-{status}",
                "status": status,
                "revision": REVISION_ID if has_result else None,
                "artifact": HASH_A if has_result else None,
                "error_code": "fixture_stopped" if has_error else None,
                "error_message": "fixture did not complete" if has_error else None,
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )


@pytest.mark.parametrize(
    ("state", "response_status", "has_object"),
    [
        ("in_progress", None, False),
        ("succeeded", 201, True),
        ("terminal_failure", 409, False),
    ],
)
def test_publication_request_accepts_exact_contract_states(
    v2_engine, state, response_status, has_object
):
    terminal = response_status is not None
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        connection.execute(
            sa.text(
                "INSERT INTO published_objects "
                "(object_hash, payload_bytes, byte_length, media_type, schema_id, "
                "schema_version, canonicalization, created_at) VALUES "
                "(:hash, :payload, 2, 'application/json', :schema_id, "
                ":schema_version, 'RFC8785', :created_at)"
            ),
            {
                "hash": HASH_B,
                "payload": b"{}",
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "created_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "INSERT INTO publication_requests "
                "(id, operation, idempotency_key, revision_id, "
                "request_fingerprint, state, response_status, response_body, "
                "response_headers_json, published_object_hash, created_at, "
                "updated_at) VALUES (:id, 'publication', :key, :revision, "
                ":fingerprint, :state, :response_status, :response_body, "
                ":response_headers, :object_hash, :created_at, :updated_at)"
            ),
            {
                "id": f"72000000-0000-4000-8000-{len(state):012d}",
                "key": f"publication-key-{state}",
                "revision": REVISION_ID,
                "fingerprint": HASH_A,
                "state": state,
                "response_status": response_status,
                "response_body": b"{}" if terminal else None,
                "response_headers": json.dumps({}) if terminal else None,
                "object_hash": HASH_B if has_object else None,
                "created_at": STAMP,
                "updated_at": STAMP,
            },
        )


def test_utc_shape_is_enforced_on_direct_sql(v2_engine):
    with v2_engine.begin() as connection:
        _insert_component_revision(connection)
        _insert_slot(connection)
        _insert_claim(connection, finalized=True, fingerprint=HASH_B)
    # PostgreSQL parses text before triggers and TIMESTAMPTZ intentionally
    # normalizes an offset-less literal in the session timezone.  SQLite keeps
    # timestamp text verbatim, so its database trigger can additionally demand
    # the canonical trailing Z.  In both cases malformed direct SQL is rejected;
    # UtcTimestamp rejects naive application-bound datetime values above.
    invalid_timestamp = (
        "not-a-timestamp"
        if v2_engine.dialect.name == "postgresql"
        else "2026-08-11T00:00:00"
    )
    _assert_integrity_error(
        v2_engine,
        "INSERT INTO claim_review_events_v2 "
        "(id, revision_id, claim_id, claim_fingerprint, decision, reviewed_by, "
        "note, reviewed_at, applied_draft_version, created_at) "
        "VALUES (:id, :revision, :claim, :fingerprint, 'accepted', 'reviewer', "
        "'checked', :reviewed_at, 1, :created_at)",
        {
            "id": REVIEW_ID,
            "revision": REVISION_ID,
            "claim": CLAIM_ID,
            "fingerprint": HASH_B,
            "reviewed_at": invalid_timestamp,
            "created_at": STAMP,
        },
    )


def test_downgrade_rejects_sealed_v2_and_allows_unused_schema(tmp_path):
    unused_url = f"sqlite:///{tmp_path / 'unused.db'}"
    unused_config = migration_config(unused_url)
    command.upgrade(unused_config, "head")
    command.downgrade(unused_config, PREVIOUS_HEAD)

    sealed_url = f"sqlite:///{tmp_path / 'sealed.db'}"
    sealed_config = migration_config(sealed_url)
    command.upgrade(sealed_config, "head")
    engine = sqlite_engine(sealed_url)
    with engine.begin() as connection:
        _insert_component_revision(connection)
        connection.execute(
            sa.text(
                "INSERT INTO published_objects "
                "(object_hash, payload_bytes, byte_length, media_type, schema_id, "
                "schema_version, canonicalization, created_at) VALUES "
                "(:hash, :payload, 2, 'application/json', :schema_id, "
                ":schema_version, 'RFC8785', :created_at)"
            ),
            {
                "hash": HASH_B,
                "payload": b"{}",
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "created_at": STAMP,
            },
        )
        connection.execute(
            sa.text(
                "UPDATE component_revisions SET status='published', "
                "publication_integrity='sealed_v2', content_hash=:hash, "
                "published_object_hash=:hash, published_at=:published_at "
                "WHERE id=:id"
            ),
            {
                "hash": HASH_B,
                "published_at": STAMP,
                "id": REVISION_ID,
            },
        )
    engine.dispose()
    with pytest.raises(RuntimeError, match="sealed v2"):
        command.downgrade(sealed_config, PREVIOUS_HEAD)


@pytest.mark.skipif(
    not os.getenv("PROMETHEUS_TEST_POSTGRES_URL"),
    reason="PROMETHEUS_TEST_POSTGRES_URL is not configured",
)
def test_postgresql_17_semantic_suite_is_explicitly_enabled():
    # The CI task supplies an isolated PostgreSQL 17 database.  Keeping this
    # explicit prevents a configured production-conformance target from being
    # silently skipped.
    assert os.environ["PROMETHEUS_TEST_POSTGRES_URL"].startswith(
        ("postgresql://", "postgresql+psycopg://")
    )
