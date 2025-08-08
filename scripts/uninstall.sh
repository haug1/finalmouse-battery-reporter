#!/usr/bin/env bash
set -euo pipefail

BIN_DEST="${XDG_BIN_HOME:-$HOME/.local/bin}/finalmouse_battery_reporter"
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT_DEST="$SYSTEMD_USER_DIR/finalmouse-battery-reporter.service"

systemctl --user disable --now finalmouse-battery-reporter.service || true

rm -f "$UNIT_DEST"
rm -f "$BIN_DEST"

systemctl --user daemon-reload

echo "Removed binary: $BIN_DEST"
echo "Removed service: $UNIT_DEST"
