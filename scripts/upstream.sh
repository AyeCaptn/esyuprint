#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
command -v gh >/dev/null
./build/esyuprint compatibility --json
for project in cups pycups system-config-printer; do
  gh api "repos/OpenPrinting/$project/tags?per_page=1" \
    --jq ".[0] | {project: \"$project\", tag: .name, commit: .commit.sha}"
done
