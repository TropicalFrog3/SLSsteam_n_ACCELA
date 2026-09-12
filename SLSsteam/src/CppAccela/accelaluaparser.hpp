#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/**
 * CppAccela::LuaParser
 *
 * Full native parser for SLSsteam Lua plugin files
 * (e.g. $STEAM_ROOT/config/stplug-in/<appid>.lua).
 *
 * Mirrors ACCELA's Python process_zip_task._parse_lua() and
 * _extract_app_token() exactly, including all edge cases.
 *
 * Lua plugin format (typical example):
 *
 *   addappid(1234560, 1, 0)                  -- Game Title
 *   addappid(1234561, 1, "aabb...00")        -- Base Game [WINDOWS]
 *   addappid(1234562, 1, "ccdd...00")        -- Depot 2
 *   addappid(9999901)                        -- some DLC
 *   setManifestid( 1234561 , "9876543210987654321" , 4096000000 )
 *   setManifestid( 1234562 , "1234567890123456789" )
 *   addtoken( 1234560 , "123456789012345" )
 */
namespace CppAccela::LuaParser
{
    // ── Data types ────────────────────────────────────────────────────────────

    struct DepotInfo
    {
        std::string depotId;
        std::string key;         ///< Hex depot decryption key from 3rd addappid arg
        std::string description; ///< Trailing -- comment, or "Depot <id>"
        std::string manifestGid; ///< From setManifestid(), empty if not present
        std::string sizeBytes;   ///< From setManifestid() 3rd arg, empty if absent
    };

    struct ParseResult
    {
        // Core identity
        std::string appId;
        std::string gameName;         ///< From trailing -- comment on first addappid
        std::string appToken;         ///< From addtoken(<id>, "<token>")

        // Depots: depotId → DepotInfo  (have a decryption key)
        std::unordered_map<std::string, DepotInfo> depots;

        // DLCs: depotId → description  (no decryption key)
        std::unordered_map<std::string, std::string> dlcs;

        // Manifests from setManifestid(): depotId → manifestGid
        // (also stored per-depot in DepotInfo.manifestGid for convenience)
        std::unordered_map<std::string, std::string> manifests;

        // Sizes from setManifestid() 3rd arg: depotId → bytes string
        std::unordered_map<std::string, std::string> manifestSizes;

        bool valid = false; ///< false if no addappid() found at all
    };

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Parse a complete Lua plugin file content and return structured data.
     *
     * On success, result.valid == true and result.appId is non-empty.
     * On failure (no addappid found), result.valid == false.
     */
    ParseResult parse(const std::string& luaContent);

    /**
     * Read the Lua plugin at `luaPath` and parse it.
     * Returns an invalid ParseResult if the file cannot be read.
     */
    ParseResult parseFile(const std::string& luaPath);

} // namespace CppAccela::LuaParser
