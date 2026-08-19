from __future__ import annotations

from app.jsonld_extraction import extract_product_identity


def _html(script_body: str) -> bytes:
    return (
        "<html><head><script type=\"application/ld+json\">"
        + script_body
        + "</script></head><body></body></html>"
    ).encode("utf-8")


def test_extracts_manufacturer_and_part_number_from_plain_product_object():
    html = _html(
        '{"@context":"https://schema.org","@type":"Product",'
        '"name":"Widget","brand":{"@type":"Brand","name":"Acme"},'
        '"mpn":"AB-1234"}'
    )
    identity = extract_product_identity(html)
    assert identity is not None
    assert identity.manufacturer == "Acme"
    assert identity.part_number == "AB-1234"


def test_prefers_mpn_over_sku_over_name_for_part_number():
    html = _html(
        '{"@type":"Product","name":"Widget","sku":"SKU-1","mpn":"MPN-1",'
        '"brand":"Acme"}'
    )
    identity = extract_product_identity(html)
    assert identity.part_number == "MPN-1"

    html_sku_only = _html(
        '{"@type":"Product","name":"Widget","sku":"SKU-1","brand":"Acme"}'
    )
    assert extract_product_identity(html_sku_only).part_number == "SKU-1"


def test_handles_string_brand_not_just_object():
    html = _html('{"@type":"Product","name":"Widget","brand":"Acme","sku":"S1"}')
    identity = extract_product_identity(html)
    assert identity.manufacturer == "Acme"


def test_handles_graph_wrapped_document():
    html = _html(
        '{"@context":"https://schema.org","@graph":['
        '{"@type":"WebPage","name":"irrelevant"},'
        '{"@type":"Product","name":"Widget","brand":{"name":"Acme"},"mpn":"M1"}'
        "]}"
    )
    identity = extract_product_identity(html)
    assert identity is not None
    assert identity.manufacturer == "Acme"
    assert identity.part_number == "M1"


def test_handles_array_of_top_level_entries():
    html = _html(
        '[{"@type":"BreadcrumbList"},'
        '{"@type":"Product","name":"Widget","brand":{"name":"Acme"},"sku":"S9"}]'
    )
    identity = extract_product_identity(html)
    assert identity is not None
    assert identity.part_number == "S9"


def test_multiple_types_array_matches_product():
    html = _html(
        '{"@type":["Product","Vehicle"],"name":"Widget","brand":{"name":"Acme"}}'
    )
    identity = extract_product_identity(html)
    assert identity is not None
    assert identity.manufacturer == "Acme"


def test_ignores_non_product_types():
    html = _html('{"@type":"Article","name":"Not a product"}')
    assert extract_product_identity(html) is None


def test_tolerates_malformed_json_without_raising():
    html = _html("{not valid json at all")
    assert extract_product_identity(html) is None


def test_no_script_block_returns_none():
    assert extract_product_identity(b"<html><body>nothing here</body></html>") is None


def test_ignores_product_entry_with_no_usable_identity_fields():
    html = _html('{"@type":"Product","description":"just a description"}')
    assert extract_product_identity(html) is None


def test_bounded_scan_does_not_scan_beyond_max_blocks():
    junk_blocks = "".join(
        f'<script type="application/ld+json">{{"@type":"Article","name":"junk{i}"}}</script>'
        for i in range(25)
    )
    real_block = (
        '<script type="application/ld+json">'
        '{"@type":"Product","name":"Widget","brand":{"name":"Acme"},"sku":"S1"}'
        "</script>"
    )
    html = ("<html><head>" + junk_blocks + real_block + "</head></html>").encode("utf-8")
    # The real Product block is the 26th script tag, past the 20-block scan
    # bound, so nothing is extracted -- proves the bound is real, not just
    # documented.
    assert extract_product_identity(html) is None
