{
  pkgs,
  config,
  lib,
  ...
}: let
  inherit (lib.options) mkOption;
  inherit (lib) types;
in {
  options.services.sls-steam = {
    config = mkOption {
      type = types.submodule {
        options = {
          DisableFamilyShareLock = mkOption {
            type = types.bool;
            default = true;
            description = "Disables Family Share license locking for self and others";
          };

          UseWhitelist = mkOption {
            type = types.bool;
            default = false;
            description = "Switches to whitelist instead of the default blacklist";
          };

          AutoFilterList = mkOption {
            type = types.bool;
            default = true;
            description = ''
              Automatically filter Apps in CheckAppOwnership. Filters everything but Games and Applications. Should not affect DLC checks
              Overrides black-/whitelist. Gets overriden by AdditionalApps
            '';
          };

          AppIds = mkOption {
            type = types.listOf types.int;
            default = [];
            description = "List of AppIds to ex-/include";
            example = [480];
          };

          PlayNotOwnedGames = mkOption {
            type = types.bool;
            default = false;
            description = "Enables playing of not owned games. Respects black-/whitelist AppIds";
          };

          AdditionalApps = mkOption {
            type = types.listOf types.int;
            default = [];
            description = "Additional AppIds to inject (Overrides your black-/whitelist & also overrides OwnerIds for apps you got shared!) Best to use this only on games NOT in your library.";
            example = [480];
          };

          DlcData = mkOption {
            type = types.attrsOf (types.attrsOf types.str);
            default = {};
            description = ''
              Extra Data for Dlcs belonging to a specific AppId. Only needed
              when the App you're playing is hit by Steams 64 DLC limit
            '';
            example = {
              "480" = {
                "447130" = "ticket test DLC";
                "110902" = "pieterw test DLC";
              };
            };
          };

          SafeMode = mkOption {
            type = types.bool;
            default = false;
            description = ''
              Automatically disable SLSsteam when steamclient.so does not match a predefined file hash that is known to work
              You should enable this if you're planing to use SLSsteam with Steam Deck's gamemode
            '';
          };

          WarnHashMissmatch = mkOption {
            type = types.bool;
            default = false;
            description = ''
              Warn user via notification when steamclient.so hash differs from known safe hash
              Mostly useful for development so I don't accidentally miss an update
            '';
          };

          ExtendedLogging = mkOption {
            type = types.bool;
            default = false;
            description = "Logs all calls to Steamworks (this makes the logfile huge! Only useful for debugging/analyzing";
          };
        };
      };
      description = ''
        Configuration for SLSsteam, written to ~/.config/SLSsteam/config.yaml
      '';
    };
  };

  config = {
    xdg.configFile."SLSsteam/config.yaml".source =
      (pkgs.formats.yaml {}).generate "config.yaml" config.services.sls-steam.config;
  };
}
