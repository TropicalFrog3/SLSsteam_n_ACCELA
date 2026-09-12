#pragma once

#include "accelaluaparser.hpp"

#include <cstdint>
#include <string>
#include <vector>

/**
 * CppAccela::DepotDownloader
 *
 * Native C++ replacement for ACCELA's DownloadDepotsTask._prepare_downloads()
 * and the sequential subprocess loop that follows it.
 *
 * Responsibilities:
 *   1. Locate the dotnet runtime on the host system.
 *   2. Locate DepotDownloaderMod.dll relative to the ACCELA install.
 *   3. Write /tmp/mistwalker_keys.vdf   (<depotId>;<hexKey> per line).
 *   4. For every depot in the ParseResult, build and run:
 *        dotnet DepotDownloaderMod.dll
 *          -app       <appId>
 *          -depot     <depotId>
 *          -manifest  <manifestGid>
 *          [-manifestfile <path>]     (only if non-empty binary manifest exists)
 *          -depotkeys <keys.vdf>
 *          -max-downloads 255
 *          -dir       <downloadDir>
 *          -validate
 *          [-username <ACCELA_USERNAME>]   (only if env var is set)
 *   5. Stream stdout/stderr of each subprocess to LOG_INFO so the user
 *      can see download progress without a separate terminal window.
 *   6. Clean up /tmp/mistwalker_keys.vdf after all depots finish.
 *
 * The download directory is:
 *   <destPath>/steamapps/common/<installDir>
 * where <installDir> = lua game name sanitised for filesystem use.
 *
 * All operations run synchronously (one depot at a time, matching Python).
 */
namespace CppAccela::DepotDownloader
{
    /**
     * Result returned by run().
     */
    struct Result
    {
        bool        ok          = false; ///< true if all depots downloaded without fatal error
        uint64_t    totalBytes  = 0;     ///< sum of manifest size estimates (bytes)
        int         depotsDone  = 0;     ///< number of depots that exited 0
        int         depotsTotal = 0;     ///< total depots attempted
        std::string downloadDir;         ///< absolute path to steamapps/common/<name>
    };

    /**
     * Locate the `dotnet` binary.
     * Search order:
     *   1. $DOTNET_ROOT/dotnet
     *   2. $HOME/.dotnet/dotnet
     *   3. /usr/bin/dotnet
     *   4. /usr/local/bin/dotnet
     *   5. Iterate $PATH entries
     * Returns empty string if not found.
     */
    std::string findDotnet();

    /**
     * Locate DepotDownloaderMod.dll.
     * The DLL lives alongside ACCELA's Python source tree at:
     *   ~/.local/share/ACCELA/src/deps/DepotDownloaderMod.dll  (installed)
     * Returns empty string if not found.
     */
    std::string findDepotDownloaderDll();

    /**
     * Run the full download for all depots in `lua`.
     *
     * @param lua          Parsed Lua plugin data (from LuaParser::parse()).
     * @param destPath     Root Steam library path (e.g. ~/.local/share/Steam).
     *                     Game files go under <destPath>/steamapps/common/<name>.
     * @param manifestDir  Directory that contains pre-fetched *.manifest files.
     *                     Pass empty string to rely on CDN-only manifests.
     * @param progressPath Optional path to write JSON progress updates.
     *                     Written atomically (rename from .tmp) so the parent
     *                     can poll it safely.  Pass empty string to disable.
     */
    Result run(const LuaParser::ParseResult& lua,
               const std::string& destPath,
               const std::string& manifestDir,
               const std::string& progressPath,
               const std::vector<std::string>& selectedDepots);

} // namespace CppAccela::DepotDownloader
