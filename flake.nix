{
  description = "Finalmouse battery reporter";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }: let
    mkPackage = pkgs:
      pkgs.stdenv.mkDerivation {
        pname = "finalmouse-battery-reporter";
        version = "unstable-${self.lastModifiedDate or "19700101"}";

        src = builtins.path {
          path = ./.;
          name = "finalmouse-battery-reporter-source";
        };
        strictDeps = true;

        nativeBuildInputs = [
          pkgs.pkg-config
        ];

        buildInputs = [
          pkgs.hidapi
        ];

        buildPhase = ''
          runHook preBuild

          mkdir -p build
          gcc -Wall -Wextra -O2 \
            -I src \
            -o build/finalmouse_battery_reporter \
            src/main.c \
            $(pkg-config --cflags --libs hidapi-hidraw)

          runHook postBuild
        '';

        installPhase = ''
          runHook preInstall
          install -Dm755 build/finalmouse_battery_reporter $out/bin/finalmouse_battery_reporter
          runHook postInstall
        '';
      };
  in
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {
        inherit system;
      };

      package = mkPackage pkgs;
    in {
      packages = {
        default = package;
        finalmouse-battery-reporter = package;
      };

      devShells.default = pkgs.mkShell {
        packages = [
          pkgs.gcc
          pkgs.hidapi
          pkgs.pkg-config
        ];
      };
    })
    // {
      nixosModules.default = {
        config,
        lib,
        pkgs,
        ...
      }: let
        cfg = config.services.finalmouseBatteryReporter;
        package =
          cfg.package
          or self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        configFile = pkgs.writeText "finalmouse-battery-reporter.json" (builtins.toJSON {
          format = cfg.format;
          thresholds = cfg.thresholds;
        });
      in {
        options.services.finalmouseBatteryReporter = {
          enable = lib.mkEnableOption "the Finalmouse battery reporter user service";

          package = lib.mkOption {
            type = lib.types.package;
            default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
            description = "Package providing the finalmouse battery reporter binary.";
          };

          outputFile = lib.mkOption {
            type = lib.types.str;
            default = "%h/.cache/finalmouse/battery";
            description = "Battery reporter output file passed via FMBR_OUTPUT_FILE.";
          };

          format = lib.mkOption {
            type = lib.types.enum ["json" "text" "raw"];
            default = "raw";
            description = "Battery reporter output format written to the generated JSON config.";
          };

          thresholds = lib.mkOption {
            default = [];
            description = "Battery thresholds used to override icon, color, class, and optional tooltip for text/json output.";
            type = lib.types.listOf (lib.types.submodule {
              options = {
                percentage = lib.mkOption {
                  type = lib.types.ints.between 0 100;
                  description = "Minimum percentage matched by this threshold.";
                };

                icon = lib.mkOption {
                  type = lib.types.str;
                  description = "Icon string used by the matched threshold.";
                };

                color = lib.mkOption {
                  type = lib.types.str;
                  description = "Color string used by the matched threshold.";
                };

                class = lib.mkOption {
                  type = lib.types.str;
                  description = "Class string used by text output and JSON class.";
                };

                tooltip = lib.mkOption {
                  type = lib.types.nullOr lib.types.str;
                  default = null;
                  description = "Optional tooltip string for the matched threshold.";
                };
              };
            });
          };
        };

        config = lib.mkIf cfg.enable {
          environment.systemPackages = [package];

          services.udev.extraRules = ''
            KERNEL=="hidraw*", ATTRS{idVendor}=="361d", ATTRS{idProduct}=="0100", TAG+="uaccess"
          '';

          systemd.user.services.finalmouse-battery-reporter = {
            description = "Finalmouse Battery Reporter";
            after = ["default.target"];
            wantedBy = ["default.target"];

            serviceConfig = {
              Type = "simple";
              ExecStart = "${package}/bin/finalmouse_battery_reporter";
              Environment = [
                "FMBR_OUTPUT_FILE=${cfg.outputFile}"
                "FMBR_CONFIG_FILE=${configFile}"
              ];
              Restart = "on-failure";
              RestartSec = 2;
              StandardOutput = "null";
              StandardError = "journal";
            };
          };
        };
      };
    };
}
