from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).parents[2]
THIRD_PARTY = ROOT / "third_party"
MANIFEST_PATH = THIRD_PARTY / "manifest.json"
EXPECTED_FIELDS = {
    "path",
    "upstream",
    "commit",
    "license",
    "license_path",
    "sha256",
}
DEPENDENCIES = {
    "third_party/nlohmann-json/": {
        "upstream": "https://github.com/nlohmann/json",
        "commit": "55f93686c01528224f448c19128836e7df245f72",
        "license": "MIT",
        "license_paths": {"third_party/nlohmann-json/LICENSE.MIT"},
    },
    "third_party/ryu/": {
        "upstream": "https://github.com/ulfjack/ryu",
        "commit": "3377662b1958dbdefb679e2c110368512cccf4f6",
        "license": "Apache-2.0 OR Boost-1.0",
        "license_paths": {
            "third_party/ryu/LICENSE-Apache2",
            "third_party/ryu/LICENSE-Boost",
        },
    },
    "third_party/picosha2/": {
        "upstream": "https://github.com/okdshin/PicoSHA2",
        "commit": "161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29",
        "license": "MIT",
        "license_paths": {"third_party/picosha2/LICENSE"},
    },
}


def _manifest() -> list[dict[str, object]]:
    value = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    assert isinstance(value, list)
    assert value
    return value


def _dependency_for(path: str) -> dict[str, object]:
    matches = [
        details
        for prefix, details in DEPENDENCIES.items()
        if path.startswith(prefix)
    ]
    assert len(matches) == 1, f"unrecognized vendored path: {path}"
    return matches[0]


def _license_paths(value: object) -> set[str]:
    if isinstance(value, str):
        return {value}
    assert isinstance(value, list)
    assert all(isinstance(item, str) for item in value)
    return set(value)


def test_vendored_manifest_accounts_for_every_exact_file():
    entries = _manifest()
    manifest_paths: list[str] = []

    for entry in entries:
        assert set(entry) == EXPECTED_FIELDS
        path_value = entry["path"]
        assert isinstance(path_value, str)
        assert path_value.startswith("third_party/")
        assert ".." not in Path(path_value).parts
        assert not Path(path_value).is_absolute()
        manifest_paths.append(path_value)

    assert len(manifest_paths) == len(set(manifest_paths))
    actual_paths = {
        path.relative_to(ROOT).as_posix()
        for path in THIRD_PARTY.rglob("*")
        if path.is_file() and path != MANIFEST_PATH
    }
    assert set(manifest_paths) == actual_paths


def test_vendored_bytes_commits_and_licenses_are_pinned():
    for entry in _manifest():
        path_value = entry["path"]
        assert isinstance(path_value, str)
        dependency = _dependency_for(path_value)
        path = ROOT / path_value

        assert path.is_file()
        assert not path.is_symlink()
        assert entry["upstream"] == dependency["upstream"]
        assert entry["commit"] == dependency["commit"]
        assert entry["license"] == dependency["license"]
        assert _license_paths(entry["license_path"]) == dependency["license_paths"]
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        assert entry["sha256"] == f"sha256:{digest}"

        for license_path_value in _license_paths(entry["license_path"]):
            license_path = ROOT / license_path_value
            assert license_path.is_file()
            assert not license_path.is_symlink()
            assert license_path.stat().st_size > 0
