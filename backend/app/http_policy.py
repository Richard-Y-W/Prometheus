"""Strict bounded JSON transport policy for the Program 01A v2 API."""

from __future__ import annotations

from starlette.types import ASGIApp, Message, Receive, Scope, Send

from .canonical_json import (
    MAX_RAW_BYTES,
    CanonicalJsonError,
    canonicalize_value,
    parse_strict_json,
)


_INVALID_BODY = canonicalize_value(
    {
        "detail": {
            "code": "request_json_invalid",
            "message": "The v2 JSON request is invalid.",
        }
    }
)
_OVERSIZED_BODY = canonicalize_value(
    {
        "detail": {
            "code": "request_too_large",
            "message": "The v2 JSON request exceeds the 8 MiB limit.",
        }
    }
)


def _header(scope: Scope, name: bytes) -> bytes | None:
    for key, value in scope.get("headers", []):
        if key.lower() == name:
            return value
    return None


def _is_json_request(scope: Scope) -> bool:
    content_type = _header(scope, b"content-type")
    if content_type is None:
        return False
    media_type = (
        content_type.decode("latin-1")
        .split(";", maxsplit=1)[0]
        .strip()
        .lower()
    )
    return media_type == "application/json" or media_type.endswith("+json")


def _is_v2_path(scope: Scope) -> bool:
    path = scope.get("path", "")
    return path == "/api/v2" or path.startswith("/api/v2/")


async def _send_error(send: Send, *, status: int, body: bytes) -> None:
    await send(
        {
            "type": "http.response.start",
            "status": status,
            "headers": [
                (b"content-type", b"application/json"),
                (b"content-length", str(len(body)).encode("ascii")),
            ],
        }
    )
    await send({"type": "http.response.body", "body": body, "more_body": False})


class StrictV2JsonMiddleware:
    """Validate and replay exact v2 JSON bytes without logging body content."""

    def __init__(self, app: ASGIApp):
        self.app = app

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if (
            scope["type"] != "http"
            or not _is_v2_path(scope)
            or scope.get("method") not in {"POST", "PUT", "PATCH"}
            or not _is_json_request(scope)
        ):
            await self.app(scope, receive, send)
            return

        declared_length = _header(scope, b"content-length")
        if declared_length is not None:
            try:
                length = int(declared_length.decode("ascii"))
            except (UnicodeDecodeError, ValueError):
                await _send_error(send, status=400, body=_INVALID_BODY)
                return
            if length < 0:
                await _send_error(send, status=400, body=_INVALID_BODY)
                return
            if length > MAX_RAW_BYTES:
                await _send_error(send, status=413, body=_OVERSIZED_BODY)
                return

        chunks: list[bytes] = []
        byte_count = 0
        while True:
            message = await receive()
            if message["type"] == "http.disconnect":
                return
            if message["type"] != "http.request":
                continue
            chunk = message.get("body", b"")
            byte_count += len(chunk)
            if byte_count > MAX_RAW_BYTES:
                await _send_error(send, status=413, body=_OVERSIZED_BODY)
                return
            chunks.append(chunk)
            if not message.get("more_body", False):
                break

        source = b"".join(chunks)
        try:
            parse_strict_json(source)
        except CanonicalJsonError:
            await _send_error(send, status=400, body=_INVALID_BODY)
            return

        delivered = False

        async def replay() -> Message:
            nonlocal delivered
            if delivered:
                return {"type": "http.request", "body": b"", "more_body": False}
            delivered = True
            return {"type": "http.request", "body": source, "more_body": False}

        await self.app(scope, replay, send)


__all__ = ["StrictV2JsonMiddleware"]
