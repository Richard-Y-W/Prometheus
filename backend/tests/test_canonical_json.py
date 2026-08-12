from __future__ import annotations

from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import pytest

from app.canonical_json import (
    MAX_ARRAY_ELEMENTS,
    MAX_DEPTH,
    MAX_NODES,
    MAX_OBJECT_MEMBERS,
    MAX_RAW_BYTES,
    MAX_STRING_BYTES,
    CanonicalJsonError,
    canonicalize_json_bytes,
    canonicalize_value,
    object_hash,
    parse_strict_json,
    verify_canonical_bytes,
)


ROOT = Path(__file__).parents[2]
CORPUS = ROOT / "fixtures" / "conformance" / "rfc8785"
MANIFEST = json.loads((CORPUS / "manifest.json").read_text(encoding="utf-8"))


def test_manifest_limits_match_the_public_contract():
    assert MANIFEST["limits"] == {
        "max_raw_bytes": MAX_RAW_BYTES,
        "max_depth": MAX_DEPTH,
        "max_nodes": MAX_NODES,
        "max_object_members": MAX_OBJECT_MEMBERS,
        "max_array_elements": MAX_ARRAY_ELEMENTS,
        "max_string_bytes": MAX_STRING_BYTES,
        "safe_integer_min": -(2**53) + 1,
        "safe_integer_max": 2**53 - 1,
    }


def _node_tree(scalar_count: int) -> bytes:
    branches = []
    remaining = scalar_count
    while remaining:
        branch_size = min(remaining, MAX_ARRAY_ELEMENTS)
        branches.append(b"[" + b",".join([b"0"] * branch_size) + b"]")
        remaining -= branch_size
    return b"[" + b",".join(branches) + b"]"


def _generated_input(spec: dict[str, Any]) -> bytes:
    generator = spec["generator"]
    if generator == "nested_arrays":
        depth = spec["depth"]
        return b"[" * depth + b"0" + b"]" * depth
    if generator == "node_tree":
        return _node_tree(spec["scalar_count"])
    if generator == "object_members":
        count = spec["count"]
        members = [f'"k{index:05d}":0'.encode() for index in range(count)]
        return b"{" + b",".join(members) + b"}"
    if generator == "array_elements":
        return b"[" + b",".join([b"0"] * spec["count"]) + b"]"
    if generator == "string_bytes":
        return b'"' + b"a" * spec["count"] + b'"'
    if generator == "raw_bytes":
        return b"{}" + b" " * (spec["count"] - 2)
    raise AssertionError(f"unknown corpus generator: {generator}")


def _case_input(case: dict[str, Any]) -> bytes:
    spec = case["input"]
    if isinstance(spec, str):
        return (CORPUS / spec).read_bytes()
    if spec["kind"] == "hex":
        return bytes.fromhex(spec["data"])
    if spec["kind"] == "generated":
        return _generated_input(spec)
    raise AssertionError(f"unknown corpus input kind: {spec['kind']}")


@pytest.mark.parametrize("case", MANIFEST["success_cases"], ids=lambda case: case["id"])
def test_success_corpus(case):
    source = _case_input(case)
    expected = (CORPUS / case["canonical"]).read_bytes()
    assert canonicalize_json_bytes(source) == expected
    assert verify_canonical_bytes(expected) == expected
    assert object_hash(expected) == case["sha256"]
    assert case["sha256"] == f"sha256:{hashlib.sha256(expected).hexdigest()}"


@pytest.mark.parametrize("case", MANIFEST["failure_cases"], ids=lambda case: case["id"])
def test_failure_corpus(case):
    with pytest.raises(CanonicalJsonError) as caught:
        canonicalize_json_bytes(_case_input(case))
    assert caught.value.code == case["error_code"]


def test_configured_limits_are_inclusive():
    assert canonicalize_json_bytes(b"[" * MAX_DEPTH + b"0" + b"]" * MAX_DEPTH)
    assert canonicalize_json_bytes(_node_tree(MAX_NODES - 11))
    assert canonicalize_json_bytes(_generated_input(
        {"generator": "object_members", "count": MAX_OBJECT_MEMBERS}
    ))
    assert canonicalize_json_bytes(_generated_input(
        {"generator": "array_elements", "count": MAX_ARRAY_ELEMENTS}
    ))
    assert canonicalize_json_bytes(_generated_input(
        {"generator": "string_bytes", "count": MAX_STRING_BYTES}
    ))
    assert canonicalize_json_bytes(_generated_input(
        {"generator": "raw_bytes", "count": MAX_RAW_BYTES}
    )) == b"{}"


@pytest.mark.parametrize(
    ("value", "error_code"),
    [
        ({"value": -0.0}, "negative_zero"),
        ({"value": math.nan}, "non_finite_number"),
        ({"value": math.inf}, "non_finite_number"),
        ({"value": -math.inf}, "non_finite_number"),
        ({"value": 2**53}, "unsafe_integer"),
        ({"value": Decimal("1.25")}, "unsupported_type"),
        ({"value": object()}, "unsupported_type"),
        ({1: "not a string key"}, "non_string_key"),
        ({"value": "\ud800"}, "invalid_unicode"),
    ],
)
def test_programmatic_values_use_the_same_preflight(value, error_code):
    with pytest.raises(CanonicalJsonError) as caught:
        canonicalize_value(value)
    assert caught.value.code == error_code


@pytest.mark.parametrize("source", [b"\xef\xbb\xbf{}", b"\xef\xbb\xbf []"])
def test_utf8_bom_is_rejected(source):
    with pytest.raises(CanonicalJsonError) as caught:
        parse_strict_json(source)
    assert caught.value.code == "utf8_bom"


@pytest.mark.parametrize("source", [b"-0.0", b"-0e0", b"-0E+10"])
def test_all_negative_zero_spellings_are_rejected(source):
    with pytest.raises(CanonicalJsonError) as caught:
        canonicalize_json_bytes(source)
    assert caught.value.code == "negative_zero"


def test_nfc_and_nfd_remain_distinct():
    nfc = canonicalize_json_bytes((CORPUS / "input/nfc.json").read_bytes())
    nfd = canonicalize_json_bytes((CORPUS / "input/nfd.json").read_bytes())
    assert nfc != nfd
    assert object_hash(nfc) != object_hash(nfd)


def test_verifier_rejects_valid_but_noncanonical_json():
    with pytest.raises(CanonicalJsonError) as caught:
        verify_canonical_bytes(b'{"b":2, "a":1}')
    assert caught.value.code == "noncanonical_bytes"


def test_hashing_rejects_noncanonical_json():
    with pytest.raises(CanonicalJsonError) as caught:
        object_hash(b"{}\n")
    assert caught.value.code == "noncanonical_bytes"


def test_canonicalizer_never_emits_bytes_rejected_by_the_strict_parser():
    with pytest.raises(CanonicalJsonError) as caught:
        canonicalize_json_bytes(b"1e20")
    assert caught.value.code == "unsafe_integer"


def test_parser_rejects_non_bytes_and_trailing_content():
    with pytest.raises(CanonicalJsonError) as wrong_type:
        parse_strict_json("{}")  # type: ignore[arg-type]
    assert wrong_type.value.code == "invalid_source_type"

    with pytest.raises(CanonicalJsonError) as malformed:
        parse_strict_json(b"{}{}")
    assert malformed.value.code == "invalid_json"


def test_extreme_integer_and_depth_fail_with_stable_policy_errors():
    with pytest.raises(CanonicalJsonError) as giant_integer:
        parse_strict_json(b"9" * 5_000)
    assert giant_integer.value.code == "unsafe_integer"

    with pytest.raises(CanonicalJsonError) as extreme_depth:
        parse_strict_json(b"[" * 2_000 + b"0" + b"]" * 2_000)
    assert extreme_depth.value.code == "max_depth_exceeded"
