"""Safety-bounded outbound HTTP fetch for named/linked component acquisition
(Phase 5 checkpoint 5).

No other code in `backend/app` makes an outbound network request. This is
genuinely new, security-sensitive surface: `docs/threat-model.md` already
names it as a control that does not exist yet and lists what it needs
(scheme/host policy, redirect and download budgets, content checks, time
limits). This module is the first, deliberately narrow implementation of
that policy -- not the full future hardening list.

What this validates before ever opening a connection:

- the URL scheme is `http` or `https`;
- the hostname resolves, and every resolved address is public (not
  private/loopback/link-local/multicast/reserved/unspecified), using the
  stdlib `ipaddress` module.

What it enforces on the connection itself:

- no redirects are followed -- a 3xx response is rejected outright, not
  chased, which sidesteps the whole class of "the safe URL redirects
  somewhere unsafe" problems for this checkpoint;
- a bounded connect+read timeout;
- a strict Content-Type allow-list (`text/html` only -- the only format
  `jsonld_extraction.py` understands so far);
- the response body is streamed and capped at `settings.max_artifact_bytes`,
  aborting the read rather than buffering an unbounded payload;
- no cookies or credentials are ever sent, and the User-Agent identifies
  the tool.

Known, accepted residual risk (see docs/phase-05-component-intake.md
checkpoint 5): this validates DNS resolution *before* connecting but does
not pin the TCP connection to the validated address, so it does not fully
close a DNS-rebinding race between the check and the connect. Full IP
pinning (including correct HTTPS SNI/certificate-hostname handling) is
deferred hardening, matching the threat model's own phased framing for
Programs 02/05/09.
"""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress
import socket
from urllib.parse import urlsplit

import httpx

from .config import settings

CONNECT_TIMEOUT_S = 5.0
READ_TIMEOUT_S = 10.0
ALLOWED_SCHEMES = frozenset({"http", "https"})
ALLOWED_CONTENT_TYPES = frozenset({"text/html"})
USER_AGENT = "Prometheus-component-acquisition/1.0 (+https://github.com/Richard-Y-W/Prometheus)"


class OutboundFetchError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class FetchResult:
    bytes: bytes
    content_type: str
    final_url: str


def validate_public_address(address: str) -> None:
    """Raise unless `address` is a public, routable IP literal.

    Pure and network-free so it can be exercised directly with plain
    string examples in tests.
    """

    try:
        parsed = ipaddress.ip_address(address)
    except ValueError as exc:
        raise OutboundFetchError(
            "address_invalid", f"{address!r} is not a valid IP address"
        ) from exc
    if (
        parsed.is_private
        or parsed.is_loopback
        or parsed.is_link_local
        or parsed.is_multicast
        or parsed.is_reserved
        or parsed.is_unspecified
    ):
        raise OutboundFetchError(
            "address_not_public",
            f"the resolved address {address} is not a public address",
        )


def resolve_hostname(hostname: str, port: int) -> list[str]:
    """Resolve `hostname` to its IPv4/IPv6 literals. Injectable for tests."""

    try:
        infos = socket.getaddrinfo(hostname, port, proto=socket.IPPROTO_TCP)
    except socket.gaierror as exc:
        raise OutboundFetchError(
            "dns_resolution_failed", f"could not resolve {hostname!r}: {exc}"
        ) from exc
    addresses = sorted({info[4][0] for info in infos})
    if not addresses:
        raise OutboundFetchError(
            "dns_resolution_failed", f"{hostname!r} resolved to no addresses"
        )
    return addresses


def fetch_url_safely(
    url: str,
    *,
    client: httpx.Client | None = None,
    resolver=resolve_hostname,
) -> FetchResult:
    """Fetch `url`, enforcing the scheme/address/redirect/type/size policy
    documented at module level. Raises `OutboundFetchError` on any
    violation; never returns a partial or unvalidated result."""

    parsed = urlsplit(url)
    if parsed.scheme not in ALLOWED_SCHEMES:
        raise OutboundFetchError(
            "unsupported_scheme",
            f"URL scheme {parsed.scheme!r} is not supported; use http or https",
        )
    if not parsed.hostname:
        raise OutboundFetchError("url_invalid", "URL has no hostname")

    default_port = 443 if parsed.scheme == "https" else 80
    for address in resolver(parsed.hostname, parsed.port or default_port):
        validate_public_address(address)

    owns_client = client is None
    active_client = client or httpx.Client(
        follow_redirects=False,
        timeout=httpx.Timeout(connect=CONNECT_TIMEOUT_S, read=READ_TIMEOUT_S,
                              write=READ_TIMEOUT_S, pool=CONNECT_TIMEOUT_S),
        headers={"User-Agent": USER_AGENT},
    )
    try:
        with active_client.stream("GET", url) as response:
            if response.status_code // 100 == 3:
                raise OutboundFetchError(
                    "redirect_not_supported",
                    f"the URL responded with a redirect (status "
                    f"{response.status_code}), which is not followed",
                )
            if response.status_code != 200:
                raise OutboundFetchError(
                    "fetch_status_error",
                    f"the URL responded with HTTP status {response.status_code}",
                )
            content_type = (
                response.headers.get("content-type", "").split(";")[0].strip().lower()
            )
            if content_type not in ALLOWED_CONTENT_TYPES:
                raise OutboundFetchError(
                    "content_type_unsupported",
                    f"content type {content_type!r} is not supported yet",
                )
            declared_length = response.headers.get("content-length")
            if declared_length is not None:
                try:
                    if int(declared_length) > settings.max_artifact_bytes:
                        raise OutboundFetchError(
                            "content_too_large",
                            "the declared content length exceeds the "
                            f"{settings.max_artifact_bytes}-byte fetch limit",
                        )
                except ValueError:
                    pass
            buffer = bytearray()
            for chunk in response.iter_bytes():
                buffer += chunk
                if len(buffer) > settings.max_artifact_bytes:
                    raise OutboundFetchError(
                        "content_too_large",
                        "the response body exceeds the "
                        f"{settings.max_artifact_bytes}-byte fetch limit",
                    )
            return FetchResult(
                bytes=bytes(buffer),
                content_type=content_type,
                final_url=str(response.url),
            )
    except httpx.TimeoutException as exc:
        raise OutboundFetchError("fetch_timeout", f"the fetch timed out: {exc}") from exc
    except httpx.HTTPError as exc:
        raise OutboundFetchError(
            "fetch_network_error", f"the fetch failed: {exc}"
        ) from exc
    finally:
        if owns_client:
            active_client.close()


__all__ = [
    "FetchResult",
    "OutboundFetchError",
    "fetch_url_safely",
    "resolve_hostname",
    "validate_public_address",
]
