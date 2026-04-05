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

        src = self;
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
        };

        config = lib.mkIf cfg.enable {
          environment.systemPackages = [package];

          systemd.user.services.finalmouse-battery-reporter = {
            description = "Finalmouse Battery Reporter";
            after = ["default.target"];
            wantedBy = ["default.target"];

            serviceConfig = {
              Type = "simple";
              ExecStart = "${package}/bin/finalmouse_battery_reporter";
              Environment = "FMBR_OUTPUT_FILE=${cfg.outputFile}";
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
