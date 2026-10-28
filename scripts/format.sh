#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
uv run --no-sync ruff format src/bridge.py tests/test_bridge.py
uv run --no-sync ruff check --fix src/bridge.py tests/test_bridge.py
uv run --no-sync clang-format -i src/*.cpp src/*.h tests/*.cpp
/usr/lib/qt6/bin/qmlformat -i src/*.qml
