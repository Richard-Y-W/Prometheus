#!/usr/bin/env sh
set -eu
repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
(cd "$repo/backend" && uv sync --locked --extra dev)
(cd "$repo/frontend" && npm ci)
echo "Locked service and reference frontend dependencies are ready."
echo "Native release presets: headless-debug, integrity-debug, and desktop-no-occt-debug."
echo "Each required build must also pass its matching *-boundary-debug target-presence preset."
