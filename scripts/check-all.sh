#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/check.sh
bash bin/build
bash bin/test
./build/esyuprint compatibility --json
git diff --check
