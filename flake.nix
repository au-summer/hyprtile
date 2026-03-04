{
  description = "Hyprtile";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs =
    {
      self,
      nixpkgs,
      systems,
      ...
    }:
    let
      inherit (nixpkgs) lib;

      forSystems =
        attrs: lib.genAttrs (import systems) (system: attrs system nixpkgs.legacyPackages.${system});
    in
    {
      packages = forSystems (
        system: pkgs: {
          hyprtile =
            let
              hyprlandPkg = pkgs.hyprland;
            in
            pkgs.gcc14Stdenv.mkDerivation {
              pname = "hyprtile";
              version = "0.1";

              src = ./.;

              nativeBuildInputs = [
                pkgs.meson
                pkgs.ninja
                pkgs.pkg-config
              ]
              ++ hyprlandPkg.nativeBuildInputs;

              buildInputs = [ hyprlandPkg ] ++ hyprlandPkg.buildInputs;

              meta = with lib; {
                homepage = "https://github.com/au-summer/hyprtile";
                description = "A Hyprland plugin that extends tiling from windows to workspaces";
                license = licenses.bsd3;
                platforms = platforms.linux;
              };
            };

          default = self.packages.${system}.hyprtile;
        }
      );
    };
}
