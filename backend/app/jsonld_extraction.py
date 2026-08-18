"""Bounded extraction of `schema.org/Product` identity fields from an
HTML page's embedded JSON-LD (Phase 5 checkpoint 5).

Deliberately narrow: this proposes only identity candidates (name, brand,
sku, mpn) a human still reviews before anything is published -- it does
not attempt engineering parameters (torque, current limits, and similar),
which JSON-LD product markup essentially never carries. PDF/table
extraction and any machine-assisted (e.g. LLM-based) extraction are later,
separately scoped work.

No HTML parsing dependency is added for this: a bounded regex scan for
`<script type="application/ld+json">` blocks is sufficient and keeps the
attack surface small (no dependency that itself parses untrusted HTML into
a DOM).
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import re

_SCRIPT_PATTERN = re.compile(
    r'<script[^>]+type\s*=\s*["\']application/ld\+json["\'][^>]*>(.*?)</script>',
    re.IGNORECASE | re.DOTALL,
)
_MAX_BLOCKS = 20
_MAX_FIELD_LENGTH = 512


@dataclass(frozen=True)
class ExtractedProductIdentity:
    manufacturer: str | None
    part_number: str | None


def _bounded(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    trimmed = value.strip()
    if not trimmed:
        return None
    return trimmed[:_MAX_FIELD_LENGTH]


def _type_names(entry: dict) -> set[str]:
    raw = entry.get("@type")
    if isinstance(raw, str):
        return {raw}
    if isinstance(raw, list):
        return {item for item in raw if isinstance(item, str)}
    return set()


def _brand_name(entry: dict) -> str | None:
    brand = entry.get("brand")
    if isinstance(brand, dict):
        return _bounded(brand.get("name"))
    return _bounded(brand)


def _candidate_entries(document: object) -> list[dict]:
    if isinstance(document, dict):
        graph = document.get("@graph")
        if isinstance(graph, list):
            return [item for item in graph if isinstance(item, dict)]
        return [document]
    if isinstance(document, list):
        return [item for item in document if isinstance(item, dict)]
    return []


def extract_product_identity(html: bytes) -> ExtractedProductIdentity | None:
    """Return the first schema.org Product's identity fields found in the
    page's JSON-LD, or None if no JSON-LD Product entry is present or
    parseable. Never raises on malformed JSON-LD -- it is untrusted third
    party content and a parse failure is simply "nothing extracted," not
    an error."""

    try:
        text = html.decode("utf-8", errors="replace")
    except Exception:
        return None

    matches = list(_SCRIPT_PATTERN.finditer(text))[:_MAX_BLOCKS]
    for match in matches:
        raw_block = match.group(1).strip()
        if not raw_block:
            continue
        try:
            document = json.loads(raw_block)
        except (json.JSONDecodeError, ValueError):
            continue
        for entry in _candidate_entries(document):
            if "Product" not in _type_names(entry):
                continue
            part_number = _bounded(entry.get("mpn")) or _bounded(entry.get("sku")) \
                or _bounded(entry.get("name"))
            manufacturer = _brand_name(entry)
            if manufacturer is None and part_number is None:
                continue
            return ExtractedProductIdentity(
                manufacturer=manufacturer, part_number=part_number
            )
    return None


__all__ = ["ExtractedProductIdentity", "extract_product_identity"]
