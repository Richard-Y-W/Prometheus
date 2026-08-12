from __future__ import annotations

from copy import deepcopy
from uuid import uuid4

import pytest
import sqlalchemy as sa

from app import package_compiler_v2
from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import (
    ClaimReviewDecisionV2,
    ReviewRequestV2,
    SCHEMA_ID,
    SCHEMA_VERSION,
)
from app.database import SessionLocal
from app.fixture_pipeline_v2 import CAPABILITY_ID, FIXTURE_ID, create_fixture_draft
from app.models import uid
from app.models_v1 import Component, ComponentRevision, Manufacturer
from app.models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimSelectionV2,
    ParameterSlotV2,
    PublishedObject,
)
from app.package_compiler_v2 import (
    PackageCompilationError,
    compile_execution_component,
)
from app.review_service_v2 import review_claims


HASH_MISSING = "sha256:" + "f" * 64


def _create_draft(key: str = "fixture-compile-0001") -> str:
    with SessionLocal() as db:
        return create_fixture_draft(
            db, fixture_id=FIXTURE_ID, idempotency_key=key
        ).revision.id


def _selected_claims(revision_id: str) -> list[CandidateClaimV2]:
    with SessionLocal() as db:
        return list(
            db.scalars(
                sa.select(CandidateClaimV2)
                .join(
                    ClaimSelectionV2,
                    sa.and_(
                        ClaimSelectionV2.revision_id
                        == CandidateClaimV2.revision_id,
                        ClaimSelectionV2.claim_id == CandidateClaimV2.id,
                    ),
                )
                .where(CandidateClaimV2.revision_id == revision_id)
                .order_by(CandidateClaimV2.id)
            )
        )


def _review_all(revision_id: str) -> None:
    selected = _selected_claims(revision_id)
    review_claims(
        revision_id=revision_id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="package-compiler-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=claim.id,
                    status="accepted",
                    note="Accepted as synthetic conformance input only.",
                )
                for claim in selected
            ],
        ),
        session_factory=SessionLocal,
    )


def _create_reviewed(key: str = "fixture-compile-0001") -> str:
    revision_id = _create_draft(key)
    _review_all(revision_id)
    return revision_id


def _assert_code(code: str, operation) -> PackageCompilationError:
    with pytest.raises(PackageCompilationError) as captured:
        operation()
    assert captured.value.code == code
    return captured.value


def _minimal_revision(db) -> ComponentRevision:
    manufacturer = Manufacturer(
        name="Compiler Empty Works", normalized_name="compileremptyworks"
    )
    db.add(manufacturer)
    db.flush()
    component = Component(
        manufacturer_id=manufacturer.id,
        part_number="EMPTY-1",
        normalized_part_number="empty1",
        family="test",
        model_class="test_component",
    )
    db.add(component)
    db.flush()
    revision = ComponentRevision(
        component_id=component.id,
        revision="empty-r1",
        certification_tier="provisional",
        status="draft",
        published_at=None,
        content_hash=None,
        draft_version=1,
        contract_schema_id=SCHEMA_ID,
        contract_schema_version=SCHEMA_VERSION,
        publication_integrity="v2_draft",
        published_object_hash=None,
        supported_recipes=[CAPABILITY_ID],
        missing_information=[],
        limitations=[
            {
                "limitation_id": uid(),
                "statement": "Empty compiler test revision.",
            }
        ],
    )
    db.add(revision)
    db.flush()
    return revision


def test_compiler_returns_validated_canonical_bytes_without_mutating_draft():
    revision_id = _create_reviewed()
    stages: list[str] = []
    with SessionLocal() as db:
        revision_before = db.get(ComponentRevision, revision_id)
        before = (
            revision_before.status,
            revision_before.draft_version,
            revision_before.content_hash,
            revision_before.published_object_hash,
            revision_before.published_at,
            db.scalar(sa.select(sa.func.count()).select_from(PublishedObject)),
        )
        compiled = compile_execution_component(
            db, revision_id, stage_callback=stages.append
        )
        revision_after = db.get(ComponentRevision, revision_id)
        after = (
            revision_after.status,
            revision_after.draft_version,
            revision_after.content_hash,
            revision_after.published_object_hash,
            revision_after.published_at,
            db.scalar(sa.select(sa.func.count()).select_from(PublishedObject)),
        )

    assert compiled.value["$schema"] == SCHEMA_ID
    assert compiled.value["schema_version"] == SCHEMA_VERSION
    assert compiled.value["authority"] == {
        "package_role": "reviewed_input",
        "engineering_decision_authority": "prometheus_cpp",
        "authority_role": "input_only",
    }
    assert compiled.value["execution_readiness"] == "blocked"
    assert compiled.execution_readiness == "blocked"
    assert compiled.canonical_bytes == canonicalize_value(compiled.value)
    assert compiled.object_hash == object_hash(compiled.canonical_bytes)
    assert "content_hash" not in compiled.value
    assert before == after
    assert stages == [
        "after_package_compilation",
        "after_schema_validation",
        "after_canonicalization",
        "after_hash_computation",
        "after_byte_verification",
    ]


def test_repeated_compilation_of_unchanged_draft_is_byte_identical():
    revision_id = _create_reviewed()
    with SessionLocal() as db:
        first = compile_execution_component(db, revision_id)
        second = compile_execution_component(db, revision_id)
    assert second.value == first.value
    assert second.canonical_bytes == first.canonical_bytes
    assert second.object_hash == first.object_hash


def test_no_parameter_slots_fails_closed():
    with SessionLocal.begin() as db:
        revision_id = _minimal_revision(db).id
    with SessionLocal() as db:
        _assert_code(
            "no_parameter_slots",
            lambda: compile_execution_component(db, revision_id),
        )


def test_missing_selection_fails_before_review_or_contract_compilation():
    revision_id = _create_draft()
    with SessionLocal.begin() as db:
        selection = db.scalar(
            sa.select(ClaimSelectionV2).where(
                ClaimSelectionV2.revision_id == revision_id
            )
        )
        db.delete(selection)
    with SessionLocal() as db:
        _assert_code(
            "missing_selection",
            lambda: compile_execution_component(db, revision_id),
        )


def test_selected_claim_without_accepted_effective_review_fails():
    revision_id = _create_draft()
    with SessionLocal() as db:
        _assert_code(
            "effective_review_missing",
            lambda: compile_execution_component(db, revision_id),
        )


def test_effective_rejected_review_is_not_compilable():
    revision_id = _create_draft()
    selected = _selected_claims(revision_id)
    statuses = {selected[0].id: "rejected"}
    review_claims(
        revision_id=revision_id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="package-compiler-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=claim.id,
                    status=statuses.get(claim.id, "accepted"),
                    note="Compiler must honor the effective decision.",
                )
                for claim in selected
            ],
        ),
        session_factory=SessionLocal,
    )
    with SessionLocal() as db:
        _assert_code(
            "effective_review_not_accepted",
            lambda: compile_execution_component(db, revision_id),
        )


def test_mismatched_review_fingerprint_fails_even_if_decision_says_accepted(
    monkeypatch,
):
    revision_id = _create_reviewed()
    original = package_compiler_v2._load_effective_reviews

    def mismatched(*args, **kwargs):
        reviews = original(*args, **kwargs)
        first = next(iter(reviews.values()))
        first.claim_fingerprint = HASH_MISSING
        return reviews

    monkeypatch.setattr(package_compiler_v2, "_load_effective_reviews", mismatched)
    with SessionLocal() as db:
        _assert_code(
            "review_fingerprint_mismatch",
            lambda: compile_execution_component(db, revision_id),
        )


def test_cross_revision_evidence_fails_even_in_a_prepared_corrupt_read(
    monkeypatch,
):
    revision_id = _create_reviewed()
    original = package_compiler_v2._load_evidence

    def cross_revision(*args, **kwargs):
        records = original(*args, **kwargs)
        records[0].revision_id = str(uuid4())
        return records

    monkeypatch.setattr(package_compiler_v2, "_load_evidence", cross_revision)
    with SessionLocal() as db:
        _assert_code(
            "evidence_cross_revision",
            lambda: compile_execution_component(db, revision_id),
        )


def test_missing_source_artifact_fails_without_relying_on_filename():
    revision_id = _create_reviewed()
    with SessionLocal.begin() as db:
        gate = db.scalar(
            sa.select(CapabilityGateV2).where(
                CapabilityGateV2.revision_id == revision_id,
                CapabilityGateV2.capability_id == CAPABILITY_ID,
                CapabilityGateV2.required_review_type == "source_artifact",
            )
        )
        gate.satisfying_references = [HASH_MISSING]
    with SessionLocal() as db:
        _assert_code(
            "artifact_missing",
            lambda: compile_execution_component(db, revision_id),
        )


def test_unresolved_selected_capability_publication_gate_fails():
    revision_id = _create_reviewed()
    with SessionLocal.begin() as db:
        gate = db.scalar(
            sa.select(CapabilityGateV2).where(
                CapabilityGateV2.revision_id == revision_id,
                CapabilityGateV2.capability_id == CAPABILITY_ID,
                CapabilityGateV2.required_review_type == "claim_review",
            )
        )
        gate.state = "pending"
        gate.satisfying_references = []
        gate.reason = "Review was deliberately made incomplete."
    with SessionLocal() as db:
        _assert_code(
            "publication_gate_unresolved",
            lambda: compile_execution_component(db, revision_id),
        )


@pytest.mark.parametrize(
    ("schema_id", "schema_version"),
    [
        ("urn:prometheus:schema:execution-component:9.0.0", SCHEMA_VERSION),
        (SCHEMA_ID, "9.0.0"),
    ],
)
def test_unsupported_schema_identity_has_no_fallback(schema_id, schema_version):
    revision_id = _create_reviewed()
    with SessionLocal() as db:
        _assert_code(
            "unsupported_schema",
            lambda: compile_execution_component(
                db,
                revision_id,
                schema_id=schema_id,
                schema_version=schema_version,
            ),
        )


def test_invalid_contract_order_is_rejected_before_canonicalization(monkeypatch):
    revision_id = _create_reviewed()
    original = package_compiler_v2._build_execution_value

    def unordered(*args, **kwargs):
        value = original(*args, **kwargs)
        value["parameter_slots"] = list(reversed(value["parameter_slots"]))
        return value

    monkeypatch.setattr(package_compiler_v2, "_build_execution_value", unordered)
    with SessionLocal() as db:
        _assert_code(
            "contract_invalid",
            lambda: compile_execution_component(db, revision_id),
        )


def test_oversized_canonical_package_is_rejected(monkeypatch):
    revision_id = _create_reviewed()
    monkeypatch.setattr(package_compiler_v2, "MAX_PACKAGE_BYTES", 1)
    with SessionLocal() as db:
        _assert_code(
            "package_too_large",
            lambda: compile_execution_component(db, revision_id),
        )


def test_independent_byte_verification_rejects_noncanonical_compiler_output(
    monkeypatch,
):
    revision_id = _create_reviewed()
    monkeypatch.setattr(
        package_compiler_v2,
        "canonicalize_value",
        lambda _value: b'{ "noncanonical": true }',
    )
    with SessionLocal() as db:
        _assert_code(
            "package_noncanonical",
            lambda: compile_execution_component(db, revision_id),
        )


def test_blocked_gate_for_another_capability_does_not_block_selected_capability():
    revision_id = _create_reviewed()
    with SessionLocal.begin() as db:
        db.add(
            CapabilityGateV2(
                revision_id=revision_id,
                capability_id="component_input.unrelated",
                phase="publication",
                required_review_type="claim_review",
                state="blocked",
                satisfying_references=[],
                reason="This unrelated capability is intentionally blocked.",
            )
        )
    with SessionLocal() as db:
        compiled = compile_execution_component(db, revision_id)
    assert compiled.value["capability_id"] == CAPABILITY_ID
    assert all(
        gate["capability_id"] == CAPABILITY_ID
        for gate in compiled.value["gates"]
    )


def test_persisted_missing_information_and_limitation_ids_are_emitted_unchanged():
    revision_id = _create_reviewed()
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        expected_missing = deepcopy(revision.missing_information)
        expected_limitations = deepcopy(revision.limitations)
        compiled = compile_execution_component(db, revision_id)
    assert sorted(
        compiled.value["missing_information"],
        key=lambda item: item["missing_information_id"],
    ) == sorted(expected_missing, key=lambda item: item["missing_information_id"])
    assert sorted(
        compiled.value["limitations"], key=lambda item: item["limitation_id"]
    ) == sorted(expected_limitations, key=lambda item: item["limitation_id"])
