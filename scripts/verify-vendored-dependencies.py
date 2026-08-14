#!/usr/bin/env python3
"""Verify that checked-in third-party source bytes match their manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import NoReturn


EXPECTED_FIELDS = {
    "path",
    "upstream",
    "commit",
    "license",
    "license_path",
    "sha256",
}
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}\Z")
DIGEST_PATTERN = re.compile(r"sha256:[0-9a-f]{64}\Z")


class VerificationError(RuntimeError):
    """Raised when the vendored dependency manifest fails closed."""


def _reject(message: str) -> NoReturn:
    raise VerificationError(message)


def _manifest_path(root: Path) -> Path:
    return root / "third_party" / "manifest.json"


def _safe_path(root: Path, value: object, field: str) -> tuple[str, Path]:
    if not isinstance(value, str) or not value:
        _reject(f"{field} must be a non-empty repository-relative path")
    if "\\" in value:
        _reject(f"{field} must use forward slashes: {value}")

    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        _reject(f"{field} escapes the repository: {value}")
    canonical = relative.as_posix()
    if canonical != value or not relative.parts or relative.parts[0] != "third_party":
        _reject(f"{field} is not a canonical third_party path: {value}")
    if canonical == "third_party/manifest.json":
        _reject(f"{field} must not name the manifest itself")

    return canonical, root.joinpath(*relative.parts)


def _license_paths(root: Path, value: object) -> list[tuple[str, Path]]:
    if isinstance(value, str):
        values = [value]
    elif isinstance(value, list) and value and all(
        isinstance(item, str) for item in value
    ):
        values = value
    else:
        _reject("license_path must be a non-empty string or list of strings")
    return [_safe_path(root, item, "license_path") for item in values]


def _load_manifest(root: Path) -> list[dict[str, object]]:
    manifest_path = _manifest_path(root)
    if manifest_path.is_symlink() or not manifest_path.is_file():
        _reject(f"manifest does not exist as a regular file: {manifest_path}")
    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _reject(f"cannot load vendored manifest: {error}")
    if not isinstance(value, list) or not value:
        _reject("vendored manifest must be a non-empty JSON array")
    if not all(isinstance(entry, dict) for entry in value):
        _reject("every vendored manifest entry must be an object")
    return value


def _actual_files(root: Path) -> set[str]:
    third_party = root / "third_party"
    if not third_party.is_dir() or third_party.is_symlink():
        _reject(f"third_party does not exist as a regular directory: {third_party}")

    result: set[str] = set()
    manifest_path = _manifest_path(root)
    for path in third_party.rglob("*"):
        if path.is_symlink():
            _reject(
                "vendored paths must not be symbolic links: "
                + path.relative_to(root).as_posix()
            )
        if path.is_file() and path != manifest_path:
            result.add(path.relative_to(root).as_posix())
    return result


def verify_repository(root: Path) -> int:
    root = root.resolve()
    entries = _load_manifest(root)
    manifest_paths: list[str] = []
    normalized_entries: list[
        tuple[dict[str, object], str, Path, list[tuple[str, Path]]]
    ] = []

    for index, entry in enumerate(entries):
        if set(entry) != EXPECTED_FIELDS:
            missing = sorted(EXPECTED_FIELDS - set(entry))
            unexpected = sorted(set(entry) - EXPECTED_FIELDS)
            _reject(
                f"manifest entry {index} has invalid fields; "
                f"missing={missing}, unexpected={unexpected}"
            )

        path_value, path = _safe_path(root, entry["path"], "path")
        if path_value in manifest_paths:
            _reject(f"duplicate manifest path: {path_value}")
        manifest_paths.append(path_value)

        commit = entry["commit"]
        if not isinstance(commit, str) or COMMIT_PATTERN.fullmatch(commit) is None:
            _reject(
                f"commit for {path_value} must be a 40 lowercase hexadecimal ID"
            )
        if not isinstance(entry["upstream"], str) or not entry["upstream"]:
            _reject(f"upstream for {path_value} must be a non-empty string")
        if not isinstance(entry["license"], str) or not entry["license"]:
            _reject(f"license for {path_value} must be a non-empty string")

        digest = entry["sha256"]
        if not isinstance(digest, str) or DIGEST_PATTERN.fullmatch(digest) is None:
            _reject(f"sha256 for {path_value} must be sha256:<64 lowercase hex>")
        normalized_entries.append(
            (entry, path_value, path, _license_paths(root, entry["license_path"]))
        )

    actual_paths = _actual_files(root)
    listed_paths = set(manifest_paths)
    missing_paths = sorted(listed_paths - actual_paths)
    if missing_paths:
        _reject(f"listed vendored file does not exist: {missing_paths[0]}")
    unlisted_paths = sorted(actual_paths - listed_paths)
    if unlisted_paths:
        _reject(f"unlisted vendored file: {unlisted_paths[0]}")

    for entry, path_value, path, licenses in normalized_entries:
        if path.is_symlink() or not path.is_file():
            _reject(f"vendored path is not a regular file: {path_value}")
        actual_digest = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_digest != entry["sha256"]:
            _reject(f"SHA-256 mismatch for {path_value}")

        for license_value, license_path in licenses:
            if license_path.is_symlink() or not license_path.is_file():
                _reject(f"license file does not exist: {license_value}")
            if license_path.stat().st_size == 0:
                _reject(f"license file is empty: {license_value}")

    return len(entries)


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of scripts/)",
    )
    return parser.parse_args()


def main() -> int:
    try:
        count = verify_repository(_arguments().root)
    except (OSError, VerificationError) as error:
        print(f"vendored dependency verification failed: {error}", file=sys.stderr)
        return 1
    print(f"{count} vendored files verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
