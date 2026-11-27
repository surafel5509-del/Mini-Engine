#!/usr/bin/env bash
# Desktop build + smoke test for the C++ engine core (no Android SDK needed).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== Building engine smoke test =="
g++ -std=c++17 -Wall -Wextra -I engine/include \
    engine/src/*/*.cpp engine/tests/smoke_test.cpp \
    -o /tmp/mini_smoke -pthread

echo "== Running smoke test =="
/tmp/mini_smoke
echo "OK"
