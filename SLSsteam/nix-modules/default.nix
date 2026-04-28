{
  rev,
  lib,
  stdenv,
  pkgs,
  buildDotnetModule,
  dotnetCorePackages,
}: let
  ticketGrabber = import ./ticket-grabber.nix {
    inherit rev buildDotnetModule dotnetCorePackages;
  };
in
  pkgs.pkgsi686Linux.stdenv.mkDerivation {
    pname = "SLSsteam";
    version = "${rev}";
    src = ../.;

    nativeBuildInputs = with pkgs; [
      pkg-config
      makeWrapper
    ];

    buildInputs = with pkgs.pkgsi686Linux; [
      openssl
      which
      curl
    ];

    buildPhase = ''
      make clean

      mkdir -p tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish
      cp ${ticketGrabber}/bin/ticket-grabber \
       tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber

      make
    '';

    installPhase = ''
      mkdir -p $out/
      cp bin/SLSsteam.so $out/
      cp bin/library-inject.so $out/

      patchelf --set-rpath ${
        lib.makeLibraryPath [
          pkgs.pkgsi686Linux.curl
        ]
      } $out/SLSsteam.so
    '';

    meta = {
      description = "Steamclient Modification for Linux";
      homepage = "https://github.com/AceSLS/SLSsteam";
      license = lib.licenses.agpl3Only;
      platforms = lib.platforms.linux;
    };
  }
