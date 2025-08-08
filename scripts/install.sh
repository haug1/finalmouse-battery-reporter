#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN_SRC="$ROOT_DIR/build/finalmouse_battery_reporter"
BIN_DEST="${XDG_BIN_HOME:-$HOME/.local/bin}/finalmouse_battery_reporter"
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT_SRC="$ROOT_DIR/systemd/finalmouse-battery-reporter.service"
UNIT_DEST="$SYSTEMD_USER_DIR/finalmouse-battery-reporter.service"
DEFAULT_OUTPUT_FILE="${XDG_CACHE_HOME:-$HOME/.cache}/finalmouse/battery"

"$ROOT_DIR/scripts/build.sh"

mkdir -p "$(dirname "$BIN_DEST")"
install -m 0755 "$BIN_SRC" "$BIN_DEST"

mkdir -p "$SYSTEMD_USER_DIR"
mkdir -p "$(dirname "$DEFAULT_OUTPUT_FILE")"
install -m 0644 "$UNIT_SRC" "$UNIT_DEST"

systemctl --user daemon-reload
systemctl --user enable --now finalmouse-battery-reporter.service

echo "Installed binary: $BIN_DEST"
echo "Installed service: $UNIT_DEST"
echo "Output file: $DEFAULT_OUTPUT_FILE"
echo "Service status:"
systemctl --user --no-pager --full status finalmouse-battery-reporter.service || true
