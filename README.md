# finalmouse-battery-reporter

Reports Finalmouse battery level via HID and writes raw, text, or JSON output to a cache file.

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
- `FMBR_OUTPUT_FORMAT`: output format, one of `raw` (default), `text`, or `json`.
- `FMBR_CONFIG_FILE`: optional JSON config file used to set `format` and `thresholds`.

By default, the output file contains only the numeric percentage:

```text
42
```

When `FMBR_OUTPUT_FORMAT=text`, the output is Waybar/i3blocks-style:

```text
42% 
42%
medium
```

When `FMBR_OUTPUT_FORMAT=json`, the output is a single JSON object:

```json
{"text":"42%","tooltip":"42%","class":"medium","percentage":42,"icon":"","color":"#ffd60a"}
```

The JSON shape is designed to work across Waybar custom modules and Noctalia `custombutton`:

- Waybar uses `text`, `tooltip`, `class`, and `percentage`.
- Noctalia uses `text`, `icon`, `color`, and `tooltip`.

If `FMBR_CONFIG_FILE` is unset, `text` and `json` use built-in defaults with Unicode battery icons, hex colors, and classes `full`, `high`, `medium`, `low`, and `critical`.

JSON config files look like this:

```json
{
  "format": "json",
  "thresholds": [
    { "percentage": 100, "icon": "battery-4", "color": "none", "class": "full" },
    { "percentage": 80, "icon": "battery-3", "color": "none", "class": "high" },
    { "percentage": 60, "icon": "battery-2", "color": "none", "class": "medium" },
    { "percentage": 40, "icon": "battery-1", "color": "secondary", "class": "low" },
    { "percentage": 20, "icon": "battery", "color": "error", "class": "critical", "tooltip": "Plug in soon" }
  ]
}
```

## Nix development

```bash
nix develop
```

This provides the compiler and `hidapi` development inputs needed for local builds.

## NixOS module

This flake also exports `nixosModules.default`, so a host can consume the package and
user service directly from the app repo.

Example:

```nix
{
  imports = [
    inputs.finalmouse-battery-reporter.nixosModules.default
  ];

  services.finalmouseBatteryReporter = {
    enable = true;
    outputFile = "%h/.cache/finalmouse/battery";
    format = "json";
    thresholds = [
      {
        percentage = 100;
        icon = "battery-4";
        color = "none";
        class = "full";
      }
      {
        percentage = 80;
        icon = "battery-3";
        color = "none";
        class = "high";
      }
      {
        percentage = 60;
        icon = "battery-2";
        color = "none";
        class = "medium";
      }
      {
        percentage = 40;
        icon = "battery-1";
        color = "secondary";
        class = "low";
      }
      {
        percentage = 20;
        icon = "battery";
        color = "error";
        class = "critical";
        tooltip = "Plug in soon";
      }
    ];
  };
}
```

Available NixOS module options:

- `services.finalmouseBatteryReporter.enable`
- `services.finalmouseBatteryReporter.package`
- `services.finalmouseBatteryReporter.outputFile`
- `services.finalmouseBatteryReporter.format`
- `services.finalmouseBatteryReporter.thresholds`
