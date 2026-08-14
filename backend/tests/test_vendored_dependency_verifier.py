from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).parents[2]
VERIFIER = ROOT / "scripts" / "verify-vendored-dependencies.py"
COMMIT = "0123456789abcdef0123456789abcdef01234567"


def _sha256(path: Path) -> str:
    return f"sha256:{hashlib.sha256(path.read_bytes()).hexdigest()}"


def _write_repository(tmp_path: Path) -> tuple[Path, list[dict[str, object]]]:
    package = tmp_path / "third_party" / "example"
    package.mkdir(parents=True)
    license_path = package / "LICENSE"
    source_path = package / "source.cpp"
    license_path.write_text("example license\n", encoding="utf-8")
    source_path.write_text("int example = 1;\n", encoding="utf-8")

    entries: list[dict[str, object]] = []
    for relative_path in (
        "third_party/example/LICENSE",
        "third_party/example/source.cpp",
    ):
        entries.append(
            {
                "path": relative_path,
                "upstream": "https://example.invalid/project",
                "commit": COMMIT,
                "license": "Example",
                "license_path": "third_party/example/LICENSE",
                "sha256": _sha256(tmp_path / relative_path),
            }
        )
    _write_manifest(tmp_path, entries)
    return tmp_path, entries


def _write_manifest(root: Path, entries: list[dict[str, object]]) -> None:
    (root / "third_party" / "manifest.json").write_text(
        json.dumps(entries),
        encoding="utf-8",
    )


def _run(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(VERIFIER), "--root", str(root)],
        check=False,
        capture_output=True,
        text=True,
    )


def test_checked_in_vendored_dependencies_verify():
    result = _run(ROOT)
    assert result.returncode == 0, result.stderr
    assert "12 vendored files verified" in result.stdout


@pytest.mark.parametrize(
    ("mutation", "expected_error"),
    [
        ("duplicate", "duplicate manifest path"),
        ("bad_hash", "SHA-256 mismatch"),
        ("bad_commit", "40 lowercase hexadecimal"),
        ("missing_license", "license file does not exist"),
    ],
)
def test_invalid_manifest_entries_fail_closed(
    tmp_path: Path,
    mutation: str,
    expected_error: str,
):
    root, entries = _write_repository(tmp_path)
    if mutation == "duplicate":
        entries.append(entries[0].copy())
    elif mutation == "bad_hash":
        entries[1]["sha256"] = "sha256:" + "0" * 64
    elif mutation == "bad_commit":
        entries[1]["commit"] = COMMIT.upper()
    elif mutation == "missing_license":
        entries[1]["license_path"] = "third_party/example/MISSING"
    _write_manifest(root, entries)

    result = _run(root)

    assert result.returncode == 1
    assert expected_error in result.stderr


def test_unlisted_vendored_file_fails_closed(tmp_path: Path):
    root, _ = _write_repository(tmp_path)
    (root / "third_party" / "example" / "unlisted.hpp").write_text(
        "// not in the manifest\n",
        encoding="utf-8",
    )

    result = _run(root)

    assert result.returncode == 1
    assert "unlisted vendored file" in result.stderr
