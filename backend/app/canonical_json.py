"""Strict RFC 8785 canonical JSON and content identity.

The RFC serializer is deliberately behind a Prometheus preflight boundary.  The
preflight rejects inputs that JSON and Python would otherwise accept but that
would make a durable cross-language object identity ambiguous or unsafe.
"""

from __future__ import annotations

import hashlib
import json
import math
from typing import NoReturn

import rfc8785


MAX_RAW_BYTES = 8 * 1024 * 1024
MAX_DEPTH = 64
MAX_NODES = 100_000
MAX_OBJECT_MEMBERS = 10_000
MAX_ARRAY_ELEMENTS = 10_000
MAX_STRING_BYTES = 1024 * 1024
SAFE_INTEGER_MIN = -(2**53) + 1
SAFE_INTEGER_MAX = 2**53 - 1


class CanonicalJsonError(ValueError):
    """Stable failure returned by the Prometheus canonicalization boundary."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _fail(code: str, message: str) -> NoReturn:
    raise CanonicalJsonError(code, message)


def _parse_integer(token: str) -> int:
    negative = token.startswith("-")
    digits = token[1:] if negative else token
    significant = digits.lstrip("0") or "0"
    if significant == "0" and negative:
        _fail("negative_zero", "negative zero is not permitted")
    safe_limit = str(SAFE_INTEGER_MAX)
    if len(significant) > len(safe_limit) or (
        len(significant) == len(safe_limit) and significant > safe_limit
    ):
        _fail(
            "unsafe_integer",
            "integer is outside the interoperable binary64 safe-integer range",
        )
    return int(token)


def _token_has_nonzero_digit(token: str) -> bool:
    mantissa = token.lower().split("e", maxsplit=1)[0]
    return any(character in "123456789" for character in mantissa)


def _parse_float(token: str) -> float:
    value = float(token)
    if not math.isfinite(value):
        _fail("number_overflow", "number overflows finite IEEE-754 binary64")
    if value == 0.0:
        if _token_has_nonzero_digit(token):
            _fail("number_underflow", "nonzero number underflows to binary64 zero")
        if token.startswith("-"):
            _fail("negative_zero", "negative zero is not permitted")
    return value


def _parse_constant(token: str) -> NoReturn:
    _fail("non_finite_number", f"non-finite number token is not permitted: {token}")


def _object_from_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            _fail("duplicate_key", f"duplicate decoded object key: {key!r}")
        result[key] = value
    return result


def _check_string(value: str) -> None:
    try:
        encoded = value.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise CanonicalJsonError(
            "invalid_unicode", "string contains an invalid Unicode surrogate"
        ) from exc
    if len(encoded) > MAX_STRING_BYTES:
        _fail(
            "max_string_bytes_exceeded",
            f"UTF-8 string exceeds {MAX_STRING_BYTES} bytes",
        )


def _preflight(value: object) -> None:
    # The depth entry is the number of containing arrays/objects.  A root
    # container therefore has nesting depth one; scalar leaves do not add a
    # level.  The root itself always counts as one node.
    stack: list[tuple[object, int]] = [(value, 0)]
    node_count = 0

    while stack:
        current, containing_depth = stack.pop()
        node_count += 1
        if node_count > MAX_NODES:
            _fail("max_nodes_exceeded", f"JSON value exceeds {MAX_NODES} nodes")

        current_type = type(current)
        if current is None or current_type is bool:
            continue
        if current_type is str:
            _check_string(current)
            continue
        if current_type is int:
            if current < SAFE_INTEGER_MIN or current > SAFE_INTEGER_MAX:
                _fail(
                    "unsafe_integer",
                    "integer is outside the interoperable binary64 safe-integer range",
                )
            continue
        if current_type is float:
            if not math.isfinite(current):
                _fail("non_finite_number", "non-finite number is not permitted")
            if current == 0.0 and math.copysign(1.0, current) < 0:
                _fail("negative_zero", "negative zero is not permitted")
            continue

        if current_type is dict:
            depth = containing_depth + 1
            if depth > MAX_DEPTH:
                _fail(
                    "max_depth_exceeded",
                    f"JSON nesting exceeds {MAX_DEPTH} containers",
                )
            if len(current) > MAX_OBJECT_MEMBERS:
                _fail(
                    "max_object_members_exceeded",
                    f"object exceeds {MAX_OBJECT_MEMBERS} members",
                )
            for key, child in current.items():
                if type(key) is not str:
                    _fail("non_string_key", "JSON object keys must be strings")
                _check_string(key)
                stack.append((child, depth))
            continue

        if current_type is list:
            depth = containing_depth + 1
            if depth > MAX_DEPTH:
                _fail(
                    "max_depth_exceeded",
                    f"JSON nesting exceeds {MAX_DEPTH} containers",
                )
            if len(current) > MAX_ARRAY_ELEMENTS:
                _fail(
                    "max_array_elements_exceeded",
                    f"array exceeds {MAX_ARRAY_ELEMENTS} elements",
                )
            for child in current:
                stack.append((child, depth))
            continue

        _fail(
            "unsupported_type",
            f"value of type {current_type.__name__} is not a JSON value",
        )


def parse_strict_json(source: bytes) -> object:
    """Parse UTF-8 JSON while retaining all Prometheus trust-boundary checks."""

    if type(source) is not bytes:
        _fail("invalid_source_type", "canonical JSON input must be bytes")
    if len(source) > MAX_RAW_BYTES:
        _fail(
            "max_raw_bytes_exceeded",
            f"raw JSON exceeds {MAX_RAW_BYTES} bytes",
        )
    try:
        text = source.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise CanonicalJsonError("invalid_utf8", "input is not valid UTF-8") from exc
    if text.startswith("\ufeff"):
        _fail("utf8_bom", "UTF-8 byte-order marks are not permitted")

    decoder = json.JSONDecoder(
        object_pairs_hook=_object_from_pairs,
        parse_float=_parse_float,
        parse_int=_parse_integer,
        parse_constant=_parse_constant,
        strict=True,
    )
    try:
        value = decoder.decode(text)
    except RecursionError as exc:
        raise CanonicalJsonError(
            "max_depth_exceeded", f"JSON nesting exceeds {MAX_DEPTH} containers"
        ) from exc
    except json.JSONDecodeError as exc:
        raise CanonicalJsonError("invalid_json", "input is not valid JSON") from exc

    _preflight(value)
    return value


def _serialize_preflighted(value: object) -> bytes:
    try:
        canonical = rfc8785.dumps(value)
    except rfc8785.CanonicalizationError as exc:
        raise CanonicalJsonError(
            "canonicalization_failed", "RFC 8785 serialization failed"
        ) from exc
    if len(canonical) > MAX_RAW_BYTES:
        _fail(
            "max_canonical_bytes_exceeded",
            f"canonical JSON exceeds {MAX_RAW_BYTES} bytes",
        )
    # JCS can spell a binary64 value such as 1e20 as an integer token.  The
    # emitted representation must remain inside Prometheus's stricter input
    # domain; otherwise we would create an object that our verifier rejects.
    parse_strict_json(canonical)
    return canonical


def canonicalize_value(value: object) -> bytes:
    """Validate a programmatic JSON value and return exact RFC 8785 bytes."""

    _preflight(value)
    return _serialize_preflighted(value)


def canonicalize_json_bytes(source: bytes) -> bytes:
    """Strictly parse raw JSON and return exact RFC 8785 bytes."""

    return _serialize_preflighted(parse_strict_json(source))


def verify_canonical_bytes(source: bytes) -> bytes:
    """Return *source* unchanged only when it is already strict canonical JSON."""

    canonical = canonicalize_json_bytes(source)
    if canonical != source:
        _fail("noncanonical_bytes", "JSON bytes are valid but not canonical")
    return source


def object_hash(source: bytes) -> str:
    """Hash an already-canonical byte string after independently verifying it."""

    verified = verify_canonical_bytes(source)
    return f"sha256:{hashlib.sha256(verified).hexdigest()}"


__all__ = [
    "MAX_ARRAY_ELEMENTS",
    "MAX_DEPTH",
    "MAX_NODES",
    "MAX_OBJECT_MEMBERS",
    "MAX_RAW_BYTES",
    "MAX_STRING_BYTES",
    "SAFE_INTEGER_MAX",
    "SAFE_INTEGER_MIN",
    "CanonicalJsonError",
    "canonicalize_json_bytes",
    "canonicalize_value",
    "object_hash",
    "parse_strict_json",
    "verify_canonical_bytes",
]
