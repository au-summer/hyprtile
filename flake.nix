{
  description = "Hyprtile";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      forSystems = f: nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ]
        (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forSystems (pkgs: {
        hyprtile = pkgs.gcc14Stdenv.mkDerivation {
          pname = "hyprtile";
          version = "0.1";
          src = ./.;
          nativeBuildInputs = [ pkgs.meson pkgs.ninja pkgs.pkg-config ]
            ++ pkgs.hyprland.nativeBuildInputs;
          buildInputs = [ pkgs.hyprland ] ++ pkgs.hyprland.buildInputs;
        };
        default = self.packages.${pkgs.system}.hyprtile;
      });
    };
}
