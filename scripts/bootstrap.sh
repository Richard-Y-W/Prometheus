#!/usr/bin/env sh
set -eu
repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
(cd "$repo/backend" && uv sync --extra dev)
(cd "$repo/frontend" && npm install)
echo "Service and reference frontend dependencies are ready. Use the headless CMake preset for non-UI CI."
