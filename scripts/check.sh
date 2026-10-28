#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test -f uv.lock
test -d .venv
uv run --no-sync ruff check src/bridge.py tests/test_bridge.py
uv run --no-sync ruff format --check src/bridge.py tests/test_bridge.py
uv run --no-sync clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp
for file in src/*.qml; do
  /usr/lib/qt6/bin/qmlformat "$file" | diff -u "$file" -
done
for file in bin/build bin/test bin/install scripts/*.sh pkgbuild/PKGBUILD; do
  bash -n "$file"
done
desktop-file-validate pkgbuild/esyuprint.desktop
uv run --no-sync pytest -q
