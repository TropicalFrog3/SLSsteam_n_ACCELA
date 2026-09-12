#pragma once

#include "accelaluaparser.hpp"
#include "acceladepotdownloader.hpp"

#include <string>

/**
 * CppAccela::PostProcess
 *
 * Native C++ replacement for ACCELA's CLITaskManager.run_post_processing().
 *
 * Runs the following steps in order after DepotDownloader::run() completes:
 *
 *   1. writeAcf()           — write appmanifest_<appid>.acf to steamapps/
 *   2. writeAppToken()      — write apptoken.txt to game dir (SLSsteam mode OFF)
 *   3. moveManifests()      — move non-empty *.manifest from staging → depotcache
 *   4. chmodBinaries()      — chmod +x .sh/.x86/.x86_64/.bin and ELF files
 *   5. updateSLSConfig()    — add appid + token to ~/.config/SLSsteam/config.yaml
 *
 * The single entry point run() calls all five steps and returns true if
 * the mandatory steps (ACF write) succeeded.
 *
 * SLSsteam mode is always ON here — we are a SLSsteam plugin by definition.
 */
namespace CppAccela::PostProcess
{
    /**
     * Context collected by the caller and passed to run().
     * Combines the outputs of LuaParser and DepotDownloader.
     */
    struct Context
    {
        const LuaParser::ParseResult&       lua;         ///< Parsed Lua data
        const DepotDownloader::Result&      dlResult;    ///< Download result
        std::string                         destPath;    ///< Steam library root
        std::string                         manifestDir; ///< Staging dir for manifests
        std::string                         depotcacheDir; ///< Final depotcache dir
        std::string                         configPath;  ///< Absolute path to config.yaml
                                                         ///< (resolved before fork — no g_config)
    };

    /**
     * Run all post-processing steps.
     * Returns true if the ACF was written successfully (other steps are
     * best-effort and log warnings on failure).
     */
    bool run(const Context& ctx);

    // ── Individual steps (also callable standalone) ───────────────────────────

    /** Write steamapps/appmanifest_<appid>.acf */
    bool writeAcf(const Context& ctx);

    /**
     * Write steamapps/common/<installdir>/apptoken.txt
     * Only runs when lua.appToken is non-empty.
     */
    void writeAppToken(const Context& ctx);

    /**
     * Move non-empty *.manifest files from the staging dir to depotcache.
     * Empty placeholder files (API fallback) are deleted, not moved.
     */
    void moveManifests(const Context& ctx);

    /**
     * Recursively chmod +x all .sh / .x86 / .x86_64 / .bin files and
     * all ELF binaries (magic \x7fELF, ≥ 1 KB, no extension) in the
     * game directory.
     */
    void chmodBinaries(const Context& ctx);

    /**
     * Append the game's appId (and token if present) to
     * ~/.config/SLSsteam/config.yaml under AdditionalApps / AppTokens.
     * Uses g_config.addAdditionalAppId() which is already implemented in
     * config.cpp and handles deduplication + atomic writes.
     */
    void updateSLSConfig(const Context& ctx);

} // namespace CppAccela::PostProcess
