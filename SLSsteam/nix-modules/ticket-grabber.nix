{
  buildDotnetModule,
  dotnetCorePackages,
  rev,
}: let
  shortRev = builtins.substring 0 7 rev;
in
  buildDotnetModule {
    pname = "Ticket-Grabber";
    version = "0.1.0+${shortRev}";

    src = ../tools/ticket-grabber;

    projectFile = "ticket-grabber.csproj";
    dotnet-sdk = dotnetCorePackages.sdk_9_0;
    dotnet-runtime = dotnetCorePackages.runtime_9_0;
    nugetDeps = ./deps.json;
  }
