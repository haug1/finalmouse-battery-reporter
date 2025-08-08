#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_FILE="$BUILD_DIR/finalmouse_battery_reporter"

mkdir -p "$BUILD_DIR"

gcc -Wall -Wextra -O2 -o "$OUTPUT_FILE" "$ROOT_DIR/src/main.c" -lhidapi-hidraw -lpthread

echo "Built: $OUTPUT_FILE"
