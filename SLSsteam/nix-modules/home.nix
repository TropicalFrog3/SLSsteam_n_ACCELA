{
  pkgs,
  config,
  lib,
  ...
}:
let
  inherit (lib) mkOption types;
in
{
  options.services.sls-steam.config = mkOption {
    type = types.submodule {
      freeformType = (pkgs.formats.yaml { }).type;
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
            Automatically filter Apps in CheckAppOwnership
            Filters everything but Games and Applications
            Overrides black-/whitelist. Gets overridden by AdditionalApps
          '';
        };

        AppIds = mkOption {
          type = types.listOf types.int;
          default = [ ];
          description = "List of AppIds to ex-/include";
          example = [
            440
            730
          ];
        };

        PlayNotOwnedGames = mkOption {
          type = types.bool;
          default = false;
          description = "Enables playing of not owned games. Respects black-/whitelist AppIds";
        };

        AdditionalApps = mkOption {
          type = types.listOf types.int;
          default = [ ];
          description = "Additional AppIds to inject. Overrides black-/whitelist and OwnerIds for shared apps. Best used only for games not in your library";
          example = [
            620
            400
          ];
        };

        DlcData = mkOption {
          type = types.attrsOf (types.attrsOf types.str);
          default = { };
          description = "Extra DLC data for a specific AppId. Only needed when the app hits Steam's 64 DLC limit";
          example = {
            "AppId" = {
              "FirstDlcAppId" = "Dlc Name";
              "SecondDlcAppId" = "Dlc Name";
            };
          };
        };

        AppTokens = mkOption {
          type = types.attrsOf types.int;
          default = { };
          description = "Used to retrieve ProductInfo from Steam servers for some games";
        };

        FakeOffline = mkOption {
          type = types.listOf types.int;
          default = [ ];
          description = "Fake Steam being offline for specified AppIds";
          example = [
            440
            730
          ];
        };

        FakeAppIds = mkOption {
          type = types.attrsOf types.int;
          default = { };
          description = ''
            Change AppIds of games to enable networking features. Use 0 as key to apply to all unowned apps
            Keeps track of the proper AppIds via game launches, so please do not start multiple FakeAppId enabled games simultaneously
          '';
          example = {
            "0" = 480;
          };
        };

        IdleStatus = mkOption {
          type = types.submodule {
            options = {
              AppId = mkOption {
                type = types.int;
                default = 0;
              };
              Title = mkOption {
                type = types.str;
                default = "";
              };
            };
          };
          default = { };
          description = "Custom ingame statuses";
        };

        GameTitles = mkOption {
          type = types.attrsOf types.str;
          default = { };
          description = "Override game titles. Only works with owned AppIds";
        };

        SubscriptionTimestamps = mkOption {
          type = types.attrsOf types.int;
          default = { };
          description = "Override purchase timestamps";
        };

        DenuvoGames = mkOption {
          type = types.attrsOf (types.listOf types.int);
          default = { };
          description = "Blocks games from unlocking on wrong accounts";
        };

        SafeMode = mkOption {
          type = types.bool;
          default = false;
          description = "Automatically disable SLSsteam when steamclient.so does not match a predefined file hash that is known to work
. Enable when using SLSsteam with Steam Deck gamemode";
        };

        Notifications = mkOption {
          type = types.bool;
          default = true;
        };

        WarnHashMissmatch = mkOption {
          type = types.bool;
          default = false;
          description = "Warn user via notification when steamclient.so hash differs from known safe hash
";
        };

        NotifyInit = mkOption {
          type = types.bool;
          default = true;
          description = "Notify when SLSsteam is done initializing";
        };

        API = mkOption {
          type = types.bool;
          default = false;
          description = "Enable sending commands to SLSsteam via /tmp/SLSsteam.API";
        };

        DisableCloud = mkOption {
          type = types.bool;
          default = true;
          description = "Disable cloud saves for unlocked games. Set to false if using CloudRedirect or similar.";
        };

        FakeEmail = mkOption {
          type = types.str;
          default = "";
          description = "Changes your account's E-Mail clientsided";
        };

        FakeWalletBalance = mkOption {
          type = types.int;
          default = 0;
          description = "Changes your wallet's balance clientsidedly";
        };

        LogLevel = mkOption {
          type = types.enum [
            0
            1
            2
            3
            4
            5
            6
          ];
          default = 2;
          description = ''
            Log levels:
            0 = Once, 1 = Debug, 2 = Info, 3 = NotifyShort, 4 = NotifyLong, 5 = Warn, 6 = None
          '';
        };

        ExtendedLogging = mkOption {
          type = types.bool;
          default = false;
          description = "Logs all calls to Steamworks (this makes the logfile huge! Only useful for debugging/analyzing";
        };
      };
    };
    default = { };
    description = "Configuration for SLSsteam, written to ~/.config/SLSsteam/config.yaml";
  };

  config = {
    xdg.configFile."SLSsteam/config.yaml".source =
      (pkgs.formats.yaml { }).generate "config.yaml"
        config.services.sls-steam.config;
  };
}
