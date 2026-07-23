{
  description = "Displays keypresses on screen on supported Wayland compositors";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }: let
    forAllSystems = nixpkgs.lib.genAttrs [
      "x86_64-linux"
      "aarch64-linux"
    ];
  in {
    packages = forAllSystems (system: let
      pkgs = import nixpkgs { inherit system; };
      mkWshowkeys = { pname, extraFlags ? [] }: pkgs.stdenv.mkDerivation {
        inherit pname;
        version = "0.1.0";
        src = self;
        nativeBuildInputs = with pkgs; [ meson ninja pkg-config wayland-scanner ];
        buildInputs = with pkgs; [
          cairo libinput libxkbcommon pango udev wayland wayland-protocols
        ];
        mesonFlags = [ "-Ddevpath=/dev/input/" ] ++ extraFlags;
        meta = with nixpkgs.lib; {
          description = "Displays keypresses on screen on supported Wayland compositors";
          homepage = "https://github.com/DreamMaoMao/myshowkey";
          license = licenses.gpl3Only;
          platforms = platforms.linux;
          maintainers = [ ];
        };
      };
    in {
      wshowkeys = mkWshowkeys { pname = "wshowkeys"; };
      wshowkeys-debug = mkWshowkeys { pname = "wshowkeys-debug"; extraFlags = [ "-Dwsk_debug=true" ]; };
    });

    defaultPackage = forAllSystems (system: self.packages.${system}.wshowkeys);

    devShell = forAllSystems (system: let
      pkgs = import nixpkgs { inherit system; };
    in pkgs.mkShell {
      inputsFrom = [ self.packages.${system}.wshowkeys ];
      nativeBuildInputs = with pkgs; [ meson ninja pkg-config wayland-scanner ];
      buildInputs = with pkgs; [
        cairo
        libinput
        libxkbcommon
        pango
        udev
        wayland
        wayland-protocols
      ];
    });
  };
}
