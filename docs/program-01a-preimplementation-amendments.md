# Program 01A Pre-Implementation Amendments

- Status: Required review amendments
- Applies to: `Prometheus_Program_01A_Integrity_and_Contracts_Implementation_Plan.md`
- Repository baseline reviewed: `a3e09c57539515f041c2a7b2dc59c97e63293cd0`
- Purpose: Resolve foundational contract and trust-boundary issues before Program 01A implementation begins.

## Decision

Program 01A is approved in direction but must not begin until the amendments below are incorporated into its implementation plan and acceptance tests. These changes do not expand Program 01A into physics execution or live research. They make its contracts suitable for later C++ consumption, offline reproduction, and safe publication.

## 1. Use a multidimensional project summary

Do not model project state as one mutually exclusive summary value. A project may contain a confirmed violation while also having incomplete coverage and blocked analyses.

Use separate dimensions such as:

```json
{
  "verdict": "requirements_violated",
  "coverage": "insufficient",
  "execution_state": "completed_with_blocked_work",
  "counts": {
    "satisfied_within_scope": 8,
    "violated": 2,
    "indeterminate": 3,
    "not_applicable": 1,
    "not_evaluated": 5
  }
}
```

The contract must permit known failures, insufficient coverage, and blocked work to coexist. User-facing `satisfied` language must always remain scoped to the declared requirement, scenario, model, evidence, applicability range, and numerical validity.

## 2. Adopt cross-language canonicalization

Python `json.dumps(sort_keys=True, separators=(",", ":"))` alone is not a sufficient long-term canonicalization specification for packages that C++ must independently verify.

Program 01A must either:

1. Adopt RFC 8785 JSON Canonicalization Scheme; or
2. Define an equivalently precise Prometheus canonical encoding and implement compatible Python and C++ golden tests.

The selected scheme must define object-key ordering, Unicode handling, escaping, number serialization, negative zero, exponent notation, and rejection of non-finite values.

Golden cases must include:

- Unicode keys and values.
- Escaped control characters.
- `-0.0` normalization.
- Very small and very large finite numbers.
- Integer and floating-point representations.
- Rejection of NaN and positive or negative infinity.
- Nested object and array ordering behavior.

At least one minimal C++ test must verify the same checked-in package hash produced by Python.

## 3. Publish immutable canonical bytes

Do not make mutable relational rows the sole representation of a published component package.

The required publication path is:

```text
Draft relational records
  -> complete explicit review
  -> canonical package bytes
  -> SHA-256 over those exact bytes
  -> immutable content-addressed object
  -> published revision references the object hash
```

The stored canonical bytes are the authoritative published input. Relational records remain useful for drafts, queries, and indexes. Export must return the stored package after hash verification rather than silently reconstructing a potentially different package with a newer serializer.

The database must prevent published package replacement or mutation. Detection of post-publication tampering is necessary but is not a substitute for immutability.

## 4. Review stable claim identities, not field names

Review decisions must target stable `claim_id` or `parameter_candidate_id` values. A mutable `field_name` is not a sufficient review identity and cannot distinguish conflicting candidate claims for the same property.

Program 01A may support exactly one candidate claim per field for its synthetic fixture, but the contract must state this limitation explicitly. It must not establish field-name review as the permanent model.

The future-safe relationship is:

```text
Parameter field
  -> one or more candidate claims
  -> supporting or conflicting evidence records
  -> explicit claim review
  -> selected compiled execution parameter
```

Every submitted decision must belong to the same draft revision being reviewed. Cross-revision claim IDs must fail closed.

## 5. Separate confidence dimensions

Do not retain a generic `confidence` number that could be presented as engineering confidence.

If a numerical value is required in 01A, name it `extraction_confidence` and define it narrowly as confidence in transcription or extraction. Keep these concepts separate:

- Source authority.
- Exact identity match.
- Extraction confidence.
- Review status.
- Physical validation status.
- Model completeness.
- Applicability and uncertainty.

Synthetic fixture evidence should use `null` extraction confidence unless the number has a documented interpretation. Add a database constraint requiring any populated confidence value to lie within `[0, 1]`.

## 6. Distinguish evidence classes and execution records

Manufacturer documents, user measurements, derived claims, and validation observations are input evidence. Raw solver outputs are immutable execution-result artifacts. They require provenance but have a different role.

Use evidence-class-dependent requirements. Do not permanently require a public URI, excerpt, and page locator for evidence types where those fields are inapplicable, such as private uploads, user measurements, derived values, or physical test observations.

Unknown values require a reason and may reference records describing where the system searched. They do not require evidence claiming that a numerical value exists.

## 7. Define concurrency and idempotency semantics

The publication transaction must specify behavior for concurrent review and publication requests in SQLite development and PostgreSQL production environments.

Required behavior:

- Review submissions include an expected draft version or equivalent optimistic concurrency token.
- Stale review submissions fail explicitly.
- Publication uses a persisted idempotency key and returns the original response when replayed.
- Two concurrent publication attempts produce exactly one immutable package.
- A unique constraint prevents multiple published objects for one revision.
- Transaction isolation and locking assumptions are documented.
- Failure at any publication step rolls back status, timestamp, hash, object reference, and job state together.

## 8. Strengthen database invariants

Enforce important trust rules below the HTTP handler when practical:

- Allowed revision, review, job, execution, and obligation states.
- Confidence range or null.
- Unique parameter names within a revision.
- Unique claim/evidence identities.
- Foreign-key enforcement in SQLite.
- Published package reference and content hash presence.
- Draft package reference and content hash absence.
- Published-content immutability.

Use timezone-aware database timestamps where supported and serialize them as UTC RFC 3339 at the contract boundary. Do not rely solely on application validators for persisted trust invariants.

## 9. Clarify Python support and authority wording

The repository currently declares Python `>=3.11`, while the plan names Python 3.12. Before implementation, either:

- Test and support Python 3.11 and newer; or
- Pin the repository to Python 3.12 and regenerate the documented environment and lock metadata.

The durable authority invariant should be that exactly one versioned Prometheus decision core owns confirmed engineering state, applicability, acceptance criteria, coverage, and findings. Program 01 uses the C++ core as that implementation. Numerical engines and Python workers cannot independently issue Prometheus verdicts.

## 10. Keep review gates capability-specific

The broader platform defines semantic, intent, and analysis review gates. Program 01A contracts must not imply that every future check requires every possible review.

Each capability will later declare its required review gates. Simple compatibility checks should not require irrelevant mesh or contact review. Execution remains blocked until all reviews required by the selected capability are satisfied.

## 11. Add missing acceptance tests

Add the following to Program 01A verification:

- Concurrent publication attempts.
- Idempotency-key replay across process restart.
- Package byte stability after application restart.
- Python/C++ canonical-hash agreement.
- Unicode and floating-point canonicalization cases.
- Rollback after injected failure at every publication stage.
- Rejection of mutation or deletion of published content.
- Cross-revision evidence or claim reference rejection.
- Stale review submission rejection.
- Re-review after ambiguous or rejected decisions.
- Missing, changed, or deleted source artifact handling.
- Unicode and normalization attacks against fixture identity matching.
- Empty and whitespace-only reviewer or review notes.
- Oversized review payload rejection.
- Unsupported schema-version rejection.
- Legacy-client calls to retired endpoints.

The plan may defer full C++ consumption of component parameters to Program 01B, but it must not defer verification that C++ can reproduce the package hash.

## 12. Preserve history while correcting claims

Documentation must accurately call the current system a fixture-backed vertical demonstrator. Historical milestone records should remain historical records rather than being rewritten as though the work never occurred.

Use explicit notices such as:

- Implemented at the time.
- Description superseded because it overstated maturity.
- Path retired because it did not meet the current trust boundary.

Retiring legacy Python analysis endpoints is an intentional breaking API change and must be recorded as such.

## 13. Keep execution instructions separate from design

Remove environment-specific agent-skill requirements from the durable engineering plan. Instructions such as requiring a particular agent framework or skill belong in an execution runbook, not in the Prometheus architecture or acceptance criteria.

Small coherent commits are encouraged, but exact commit boundaries are not product invariants. No task may force a commit that leaves the repository knowingly misleading or unsafe.

## 14. Scope that remains unchanged

These amendments do not authorize:

- Live web research.
- PDF or datasheet parsing.
- Motor A versus Motor B execution.
- Replacing fixed C++ motor inputs.
- New physics checks.
- External solver integration.
- Universal project intake.
- General semantic graph implementation.

Program 01A remains a trust-boundary and contract program. Program 01B remains responsible for proving that a bound, reviewed package drives C++ engineering results.

## Entry gate for Program 01A implementation

Implementation may begin when the Program 01A plan has been revised to include:

- A multidimensional project-status contract.
- A cross-language canonicalization specification.
- Immutable storage of exact published package bytes.
- Stable claim-level review identities.
- Explicit concurrency and idempotency behavior.
- Database-level trust constraints.
- Aligned Python-version support.
- The additional acceptance tests listed above.

Approval of these amendments authorizes implementation of Program 01A only. It does not imply that the resulting repository can evaluate arbitrary projects or that its published packages already drive engineering calculations.
