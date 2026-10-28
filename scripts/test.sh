#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
uv run --no-sync pytest "$@"
bash bin/build
bash bin/test
