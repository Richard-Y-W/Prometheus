"""manual component draft intake v2

Revision ID: f1a9c02e5b6d
Revises: a41f0c93e2d7
Create Date: 2026-08-15
"""

from __future__ import annotations

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op


revision: str = "f1a9c02e5b6d"
down_revision: Union[str, None] = "a41f0c93e2d7"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None

HASH_CHECK = (
    "length({column}) = 71 AND {column} LIKE 'sha256:%' "
    "AND lower({column}) = {column}"
)


def _is_sqlite() -> bool:
    return op.get_bind().dialect.name == "sqlite"


def _utc_type():
    if op.get_bind().dialect.name == "postgresql":
        return sa.DateTime(timezone=True)
    return sa.String(32)


def _hash_check(column: str) -> str:
    base = HASH_CHECK.format(column=column)
    if _is_sqlite():
        return base + f" AND substr({column}, 8) NOT GLOB '*[^0-9a-f]*'"
    return base + f" AND substring({column} from 8) ~ '^[0-9a-f]{{64}}$'"


def _uuid_check(column: str) -> str:
    base = (
        f"length({column}) = 36 AND lower({column}) = {column} AND "
        f"substr({column}, 9, 1) = '-' AND substr({column}, 14, 1) = '-' AND "
        f"substr({column}, 15, 1) = '4' AND substr({column}, 19, 1) = '-' AND "
        f"substr({column}, 20, 1) IN ('8','9','a','b') AND "
        f"substr({column}, 24, 1) = '-'"
    )
    if _is_sqlite():
        return base + f" AND replace({column}, '-', '') NOT GLOB '*[^0-9a-f]*'"
    return (
        base
        + f" AND {column} ~ '^[0-9a-f]{{8}}-[0-9a-f]{{4}}-4[0-9a-f]{{3}}-"
        "[89ab][0-9a-f]{3}-[0-9a-f]{12}$'"
    )


def _utc_checks(*columns: str) -> list[sa.CheckConstraint]:
    if not _is_sqlite():
        return []
    return [
        sa.CheckConstraint(
            f"{column} IS NULL OR (substr({column}, -1, 1) = 'Z' "
            f"AND datetime({column}) IS NOT NULL)",
            name=f"ck_{column}_utc",
        )
        for column in columns
    ]


def upgrade() -> None:
    if _is_sqlite():
        op.get_bind().exec_driver_sql("PRAGMA foreign_keys=ON")
    op.create_table(
        "manual_component_draft_jobs_v2",
        sa.Column("id", sa.String(36), primary_key=True),
        sa.Column("idempotency_key", sa.String(), nullable=False, unique=True),
        sa.Column("request_fingerprint", sa.String(71), nullable=False),
        sa.Column("status", sa.String(), nullable=False),
        sa.Column(
            "revision_id",
            sa.String(),
            sa.ForeignKey("component_revisions.id"),
            nullable=True,
        ),
        sa.Column(
            "artifact_hash",
            sa.String(71),
            sa.ForeignKey("artifact_objects_v2.object_hash"),
            nullable=True,
        ),
        sa.Column("error_code", sa.String(), nullable=True),
        sa.Column("error_message", sa.Text(), nullable=True),
        sa.Column("created_at", _utc_type(), nullable=False),
        sa.Column("updated_at", _utc_type(), nullable=False),
        sa.CheckConstraint(_uuid_check("id"), name="ck_manual_draft_job_uuid4"),
        sa.CheckConstraint(
            _hash_check("request_fingerprint"), name="ck_manual_draft_fingerprint"
        ),
        sa.CheckConstraint(
            "status IN ('queued','running','succeeded','failed','cancelled','timed_out')",
            name="ck_manual_draft_status",
        ),
        sa.CheckConstraint(
            "(status IN ('queued','running') AND revision_id IS NULL AND "
            "artifact_hash IS NULL "
            "AND error_code IS NULL AND error_message IS NULL) OR "
            "(status = 'succeeded' AND revision_id IS NOT NULL AND "
            "artifact_hash IS NOT NULL AND error_code IS NULL AND "
            "error_message IS NULL) OR (status IN "
            "('failed','cancelled','timed_out') AND revision_id IS NULL AND "
            "artifact_hash IS NULL AND error_code IS NOT NULL AND "
            "error_message IS NOT NULL)",
            name="ck_manual_draft_state_shape",
        ),
        *_utc_checks("created_at", "updated_at"),
    )


def downgrade() -> None:
    op.drop_table("manual_component_draft_jobs_v2")
