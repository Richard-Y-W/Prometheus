"""Stable semantic identity for finalized Program 01A candidate claims."""

from __future__ import annotations

from typing import Any

import sqlalchemy as sa
from sqlalchemy.orm import Session

from .canonical_json import canonicalize_value, object_hash
from .models_v2 import CandidateClaimV2, ClaimEvidenceLinkV2


def fingerprint_claim_fields(
    *,
    revision_id: str,
    slot_id: str,
    value_state: str,
    value: dict[str, object],
    provenance: str,
    evidence_ids: list[str],
    validity_conditions: list[str],
    unit: str | None,
    original_value: str | None,
    original_unit: str | None,
) -> str:
    """Hash exactly the immutable semantic fields and ordered evidence set."""

    semantic: dict[str, Any] = {
        "revision_id": revision_id,
        "slot_id": slot_id,
        "value_state": value_state,
        "value": value,
        "provenance": provenance,
        "evidence_ids": sorted(evidence_ids),
        "validity_conditions": validity_conditions,
    }
    if value_state == "known":
        semantic.update(
            {
                "unit": unit,
                "original_value": original_value,
                "original_unit": original_unit,
            }
        )
    return object_hash(canonicalize_value(semantic))


def claim_evidence_ids(db: Session, claim: CandidateClaimV2) -> list[str]:
    return list(
        db.scalars(
            sa.select(ClaimEvidenceLinkV2.evidence_id)
            .where(
                ClaimEvidenceLinkV2.revision_id == claim.revision_id,
                ClaimEvidenceLinkV2.claim_id == claim.id,
            )
            .order_by(ClaimEvidenceLinkV2.evidence_id)
        )
    )


def compute_claim_fingerprint(db: Session, claim: CandidateClaimV2) -> str:
    """Recompute a stored claim fingerprint from rows in the same revision."""

    if claim.value_state == "known":
        if not isinstance(claim.value, dict):
            raise ValueError("known claim has no structured value")
        value = claim.value
    elif claim.value_state == "unknown":
        if not isinstance(claim.reason, str) or not claim.reason:
            raise ValueError("unknown claim has no reason")
        value = {"kind": "unknown", "reason": claim.reason}
    else:
        raise ValueError(f"unsupported claim value state {claim.value_state!r}")
    return fingerprint_claim_fields(
        revision_id=claim.revision_id,
        slot_id=claim.slot_id,
        value_state=claim.value_state,
        value=value,
        provenance=claim.provenance,
        evidence_ids=claim_evidence_ids(db, claim),
        validity_conditions=list(claim.validity_conditions),
        unit=claim.unit,
        original_value=claim.original_value,
        original_unit=claim.original_unit,
    )


__all__ = [
    "claim_evidence_ids",
    "compute_claim_fingerprint",
    "fingerprint_claim_fields",
]
