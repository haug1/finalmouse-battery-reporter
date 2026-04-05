# finalmouse-battery-reporter

Reports Finalmouse battery level via HID and writes either a JSON payload or a Waybar/Vibar-compatible text payload to a cache file.

## Dependencies

- `gcc`
- `hidapi` (`libhidapi-hidraw`)
- `systemd --user`

## Build

```bash
nix build
./result/bin/finalmouse_battery_reporter
```

Or with the existing script:

```bash
./scripts/build.sh
```

Build output:

- `./build/finalmouse_battery_reporter`

## Install (recommended)

```bash
./scripts/install.sh
```

This will:

- build the binary
- install it to `~/.local/bin/finalmouse_battery_reporter`
- install and enable `~/.config/systemd/user/finalmouse-battery-reporter.service`
- start the service immediately

Default output file written by the service:

- `~/.cache/finalmouse/battery`

## Service management

```bash
systemctl --user status finalmouse-battery-reporter.service
journalctl --user -u finalmouse-battery-reporter.service -f
systemctl --user restart finalmouse-battery-reporter.service
```

## Uninstall

```bash
./scripts/uninstall.sh
```

## Configuration

Runtime environment variables:

- `FMBR_OUTPUT_FILE`: override battery output file path.
- `FMBR_OUTPUT_FORMAT`: output format, one of `json` (default), `text`, or `raw`.

By default, the output file contains:

```json
{"text":"42%","color":"#ffd60a","icon":""}
```

When `FMBR_OUTPUT_FORMAT=raw`, the output file contains only the numeric percentage:

```text
42
```

## Nix development

```bash
nix develop
```

This provides the compiler and `hidapi` development inputs needed for local builds.

## NixOS module

This flake also exports `nixosModules.default`, so a host can consume the package and
user service directly from the app repo.
