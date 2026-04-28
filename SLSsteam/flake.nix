{
  description = "SLSsteam";

  inputs.nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    forAllSystems = fn:
      nixpkgs.lib.genAttrs nixpkgs.lib.platforms.linux (
        system: let
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnfreePredicate = pkg:
              builtins.elem (nixpkgs.lib.getName pkg) ["steam" "steam-unwrapped"];
          };
        in
          fn pkgs
      );
  in {
    formatter = forAllSystems (pkgs: pkgs.alejandra);

    packages = forAllSystems (pkgs: rec {
      sls-steam = pkgs.callPackage ./nix-modules/default.nix {rev = self.rev or self.dirtyRev or "unknown";};
      wrapped = pkgs.callPackage ./nix-modules/wrapped.nix {rev = self.rev or self.dirtyRev or "unknown";};
      default = sls-steam;
    });

    homeModules = {
      sls-steam = import ./nix-modules/home.nix;
    };
  };
}
