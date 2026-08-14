"""typed execution inputs

Revision ID: 7b6d91e2a4f0
Revises: cd418805b2c6
Create Date: 2026-08-11
"""

import json
from decimal import Decimal, InvalidOperation
from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op


revision: str = "7b6d91e2a4f0"
down_revision: Union[str, None] = "cd418805b2c6"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


FIXTURE_IDENTITY = ("Prometheus Fixture Works", "PM-36-GM")
FIXTURE_METADATA = {
    "nominal_voltage_v": ("voltage", "electric_potential"),
    "continuous_torque_nm": ("torque", "torque"),
    "stall_torque_nm": ("torque", "torque"),
    "torque_constant_nm_a": ("torque_constant", "torque/electric_current"),
    "no_load_speed_rad_s": ("angular_velocity", "angle/time"),
    "no_load_current_a": ("electric_current", "electric_current"),
    "winding_resistance_ohm": ("electrical_resistance", "electric_resistance"),
    "thermal_resistance_k_w": ("thermal_resistance", "temperature/power"),
    "thermal_capacitance_j_k": ("heat_capacity", "energy/temperature"),
    "maximum_temperature_c": ("temperature_limit", "temperature"),
    "gear_ratio": ("ratio", "dimensionless"),
    "gearbox_efficiency_nominal": ("efficiency", "dimensionless"),
    "gearbox_efficiency_range": ("efficiency", "dimensionless"),
    "driver_current_limit_a": ("electric_current_limit", "electric_current"),
    "supply_current_limit_a": ("electric_current_limit", "electric_current"),
    "torque_speed_curve": ("torque_by_angular_velocity", "torque"),
    "gearbox_lifetime": ("service_life", "time"),
}


def _json_value(value):
    if isinstance(value, str):
        return json.loads(value)
    return value


def _legacy_scalar(value: str) -> dict:
    try:
        number = float(Decimal(value))
    except (InvalidOperation, ValueError):
        return {"kind": "unknown", "reason": "legacy value is not numeric"}
    return {"kind": "scalar", "value": number}


def upgrade() -> None:
    connection = op.get_bind()

    op.add_column(
        "component_revisions",
        sa.Column("supported_recipes_json", sa.JSON(), nullable=True),
    )
    op.add_column(
        "component_revisions",
        sa.Column("missing_information_json", sa.JSON(), nullable=True),
    )
    op.add_column(
        "component_revisions",
        sa.Column("limitations_json", sa.JSON(), nullable=True),
    )
    connection.execute(
        sa.text(
            "UPDATE component_revisions SET supported_recipes_json=:empty, "
            "missing_information_json=:empty, limitations_json=:empty"
        ),
        {"empty": "[]"},
    )

    op.add_column(
        "component_parameters", sa.Column("quantity", sa.String(), nullable=True)
    )
    op.add_column(
        "component_parameters", sa.Column("dimension", sa.String(), nullable=True)
    )
    op.add_column(
        "component_parameters", sa.Column("value_json", sa.JSON(), nullable=True)
    )
    op.add_column(
        "component_parameters",
        sa.Column("validity_conditions_json", sa.JSON(), nullable=True),
    )
    rows = connection.execute(
        sa.text(
            "SELECT p.id, p.field_name, p.value_si, m.name AS manufacturer, "
            "c.part_number AS part_number FROM component_parameters p "
            "JOIN component_revisions r ON r.id=p.revision_id "
            "JOIN components c ON c.id=r.component_id "
            "JOIN manufacturers m ON m.id=c.manufacturer_id"
        )
    ).mappings()
    for row in rows:
        is_fixture = (row["manufacturer"], row["part_number"]) == FIXTURE_IDENTITY
        quantity, dimension = (
            FIXTURE_METADATA.get(row["field_name"], (row["field_name"], "unknown"))
            if is_fixture
            else (row["field_name"], "unknown")
        )
        conditions = ["synthetic conformance fixture only"] if is_fixture else []
        connection.execute(
            sa.text(
                "UPDATE component_parameters SET quantity=:quantity, "
                "dimension=:dimension, value_json=:value_json, "
                "validity_conditions_json=:conditions WHERE id=:id"
            ),
            {
                "quantity": quantity,
                "dimension": dimension,
                "value_json": json.dumps(
                    _legacy_scalar(row["value_si"]), separators=(",", ":")
                ),
                "conditions": json.dumps(conditions, separators=(",", ":")),
                "id": row["id"],
            },
        )

    with op.batch_alter_table("component_parameters") as batch:
        batch.alter_column("quantity", existing_type=sa.String(), nullable=False)
        batch.alter_column("dimension", existing_type=sa.String(), nullable=False)
        batch.alter_column("value_json", existing_type=sa.JSON(), nullable=False)
        batch.alter_column(
            "validity_conditions_json", existing_type=sa.JSON(), nullable=False
        )
        batch.drop_column("value_si")

    with op.batch_alter_table("component_revisions") as batch:
        batch.alter_column(
            "supported_recipes_json", existing_type=sa.JSON(), nullable=False
        )
        batch.alter_column(
            "missing_information_json", existing_type=sa.JSON(), nullable=False
        )
        batch.alter_column("limitations_json", existing_type=sa.JSON(), nullable=False)
        batch.alter_column(
            "content_hash", existing_type=sa.String(), nullable=True
        )

    op.add_column(
        "evidence_records",
        sa.Column("confidence_numeric", sa.Numeric(4, 3), nullable=True),
    )
    op.add_column(
        "evidence_records", sa.Column("review_note", sa.String(), nullable=True)
    )
    confidence_rows = connection.execute(
        sa.text("SELECT id, confidence FROM evidence_records")
    ).mappings()
    for row in confidence_rows:
        try:
            confidence = float(Decimal(row["confidence"]))
        except (InvalidOperation, ValueError, TypeError):
            confidence = None
        connection.execute(
            sa.text(
                "UPDATE evidence_records SET confidence_numeric=:confidence "
                "WHERE id=:id"
            ),
            {"confidence": confidence, "id": row["id"]},
        )
    with op.batch_alter_table("evidence_records") as batch:
        batch.drop_column("confidence")
        batch.alter_column(
            "confidence_numeric",
            new_column_name="confidence",
            existing_type=sa.Numeric(4, 3),
            nullable=True,
        )

    op.add_column(
        "research_job_events",
        sa.Column("sequence_integer", sa.Integer(), nullable=True),
    )
    event_rows = connection.execute(
        sa.text("SELECT id, sequence FROM research_job_events")
    ).mappings()
    for row in event_rows:
        try:
            sequence = int(row["sequence"])
        except (TypeError, ValueError) as error:
            raise RuntimeError(
                f"cannot migrate non-integer research event sequence {row['sequence']!r}"
            ) from error
        connection.execute(
            sa.text(
                "UPDATE research_job_events SET sequence_integer=:sequence WHERE id=:id"
            ),
            {"sequence": sequence, "id": row["id"]},
        )
    with op.batch_alter_table("research_job_events") as batch:
        batch.drop_column("sequence")
        batch.alter_column(
            "sequence_integer",
            new_column_name="sequence",
            existing_type=sa.Integer(),
            nullable=False,
        )


def downgrade() -> None:
    connection = op.get_bind()
    parameter_rows = list(
        connection.execute(
            sa.text("SELECT id, value_json FROM component_parameters")
        ).mappings()
    )
    scalar_values: dict[str, str] = {}
    for row in parameter_rows:
        value = _json_value(row["value_json"])
        if not isinstance(value, dict) or value.get("kind") != "scalar":
            raise RuntimeError(
                "cannot downgrade non-scalar engineering value without losing meaning"
            )
        scalar_values[row["id"]] = str(value["value"])
    if connection.execute(
        sa.text("SELECT COUNT(*) FROM component_revisions WHERE content_hash IS NULL")
    ).scalar_one():
        raise RuntimeError("cannot downgrade revisions with an unset content hash")
    if connection.execute(
        sa.text("SELECT COUNT(*) FROM evidence_records WHERE confidence IS NULL")
    ).scalar_one():
        raise RuntimeError("cannot downgrade evidence with unknown confidence")

    op.add_column(
        "component_parameters", sa.Column("value_si", sa.String(), nullable=True)
    )
    for parameter_id, value in scalar_values.items():
        connection.execute(
            sa.text(
                "UPDATE component_parameters SET value_si=:value WHERE id=:id"
            ),
            {"value": value, "id": parameter_id},
        )
    with op.batch_alter_table("component_parameters") as batch:
        batch.alter_column("value_si", existing_type=sa.String(), nullable=False)
        batch.drop_column("validity_conditions_json")
        batch.drop_column("value_json")
        batch.drop_column("dimension")
        batch.drop_column("quantity")

    with op.batch_alter_table("component_revisions") as batch:
        batch.alter_column(
            "content_hash", existing_type=sa.String(), nullable=False
        )
        batch.drop_column("limitations_json")
        batch.drop_column("missing_information_json")
        batch.drop_column("supported_recipes_json")

    op.add_column(
        "evidence_records", sa.Column("confidence_text", sa.String(), nullable=True)
    )
    confidence_rows = connection.execute(
        sa.text("SELECT id, confidence FROM evidence_records")
    ).mappings()
    for row in confidence_rows:
        value = str(Decimal(str(row["confidence"])).normalize())
        connection.execute(
            sa.text(
                "UPDATE evidence_records SET confidence_text=:confidence WHERE id=:id"
            ),
            {"confidence": value, "id": row["id"]},
        )
    with op.batch_alter_table("evidence_records") as batch:
        batch.drop_column("confidence")
        batch.alter_column(
            "confidence_text",
            new_column_name="confidence",
            existing_type=sa.String(),
            nullable=False,
        )
        batch.drop_column("review_note")

    op.add_column(
        "research_job_events",
        sa.Column("sequence_text", sa.String(), nullable=True),
    )
    event_rows = connection.execute(
        sa.text("SELECT id, sequence FROM research_job_events")
    ).mappings()
    for row in event_rows:
        connection.execute(
            sa.text(
                "UPDATE research_job_events SET sequence_text=:sequence WHERE id=:id"
            ),
            {"sequence": str(row["sequence"]), "id": row["id"]},
        )
    with op.batch_alter_table("research_job_events") as batch:
        batch.drop_column("sequence")
        batch.alter_column(
            "sequence_text",
            new_column_name="sequence",
            existing_type=sa.String(),
            nullable=False,
        )
