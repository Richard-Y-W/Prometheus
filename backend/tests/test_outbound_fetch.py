from __future__ import annotations

import httpx
import pytest

from app import outbound_fetch
from app.outbound_fetch import (
    OutboundFetchError,
    fetch_url_safely,
    validate_public_address,
)


@pytest.mark.parametrize(
    "address",
    [
        "127.0.0.1",
        "127.5.5.5",
        "10.0.0.1",
        "172.16.0.1",
        "192.168.1.1",
        "169.254.169.254",
        "224.0.0.1",
        "0.0.0.0",
        "::1",
        "fc00::1",
        "fe80::1",
    ],
)
def test_validate_public_address_rejects_non_public_addresses(address):
    with pytest.raises(OutboundFetchError) as captured:
        validate_public_address(address)
    assert captured.value.code == "address_not_public"


@pytest.mark.parametrize("address", ["8.8.8.8", "1.1.1.1", "93.184.216.34"])
def test_validate_public_address_accepts_public_addresses(address):
    validate_public_address(address)


def test_validate_public_address_rejects_garbage():
    with pytest.raises(OutboundFetchError) as captured:
        validate_public_address("not-an-ip")
    assert captured.value.code == "address_invalid"


def _resolver_returning(*addresses: str):
    def resolver(hostname: str, port: int) -> list[str]:
        return list(addresses)
    return resolver


def _client_with_handler(handler) -> httpx.Client:
    return httpx.Client(
        transport=httpx.MockTransport(handler), follow_redirects=False
    )


def test_fetch_rejects_unsupported_scheme():
    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely("ftp://example.com/thing")
    assert captured.value.code == "unsupported_scheme"


def test_fetch_rejects_url_without_hostname():
    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely("https:///path")
    assert captured.value.code == "url_invalid"


def test_fetch_rejects_when_resolved_address_is_private():
    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://internal.example.com/",
            resolver=_resolver_returning("10.1.2.3"),
        )
    assert captured.value.code == "address_not_public"


def test_fetch_rejects_dns_failure():
    def failing_resolver(hostname: str, port: int) -> list[str]:
        raise OutboundFetchError("dns_resolution_failed", "no such host")

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely("https://nowhere.example.com/", resolver=failing_resolver)
    assert captured.value.code == "dns_resolution_failed"


def test_fetch_rejects_redirect():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(302, headers={"Location": "https://elsewhere.example.com/"})

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://example.com/",
            client=_client_with_handler(handler),
            resolver=_resolver_returning("93.184.216.34"),
        )
    assert captured.value.code == "redirect_not_supported"


def test_fetch_rejects_non_200_status():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(404, text="not found")

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://example.com/",
            client=_client_with_handler(handler),
            resolver=_resolver_returning("93.184.216.34"),
        )
    assert captured.value.code == "fetch_status_error"


def test_fetch_rejects_unsupported_content_type():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(
            200, headers={"Content-Type": "application/pdf"}, content=b"%PDF-"
        )

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://example.com/",
            client=_client_with_handler(handler),
            resolver=_resolver_returning("93.184.216.34"),
        )
    assert captured.value.code == "content_type_unsupported"


def test_fetch_rejects_declared_oversized_content(monkeypatch):
    monkeypatch.setattr(outbound_fetch.settings, "max_artifact_bytes", 10)

    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(
            200,
            headers={"Content-Type": "text/html", "Content-Length": "1000"},
            content=b"x" * 1000,
        )

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://example.com/",
            client=_client_with_handler(handler),
            resolver=_resolver_returning("93.184.216.34"),
        )
    assert captured.value.code == "content_too_large"


def test_fetch_rejects_streamed_oversized_content_without_declared_length(monkeypatch):
    monkeypatch.setattr(outbound_fetch.settings, "max_artifact_bytes", 10)

    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(
            200, headers={"Content-Type": "text/html"}, content=b"x" * 1000
        )

    with pytest.raises(OutboundFetchError) as captured:
        fetch_url_safely(
            "https://example.com/",
            client=_client_with_handler(handler),
            resolver=_resolver_returning("93.184.216.34"),
        )
    assert captured.value.code == "content_too_large"


def test_fetch_returns_exact_bytes_and_content_type_on_success():
    body = b"<html><body>hello</body></html>"

    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(
            200, headers={"Content-Type": "text/html; charset=utf-8"}, content=body
        )

    result = fetch_url_safely(
        "https://example.com/product",
        client=_client_with_handler(handler),
        resolver=_resolver_returning("93.184.216.34"),
    )
    assert result.bytes == body
    assert result.content_type == "text/html"
    assert result.final_url == "https://example.com/product"
