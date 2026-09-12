#pragma once

#include <string>
#include <vector>

/**
 * CppAccela::Manifest
 *
 * Implements the manifest-collection logic from accela-download.sh:
 *
 *   1. Parse a Lua plugin file to extract (depotId, manifestGid) pairs
 *      from every setManifestid() call.
 *
 *   2. Resolve manifest files:
 *        a. Look for   <depotcache>/<depotId>_<manifestGid>.manifest
 *        b. For any that are missing, call the manifest.steam.run API
 *           and create an empty placeholder file — DepotDownloaderMod
 *           will then fetch the real manifest from Steam's CDN at runtime.
 *
 *   3. Validate that the Lua file has the minimum required structure
 *      (at least one addappid() call).
 *
 * The result is a list of absolute paths ready to be packed into the ZIP
 * that accela-download passes to ACCELA's CLI.
 */
namespace CppAccela::Manifest
{
    /**
     * A single (depotId, manifestGid) pair extracted from the Lua plugin.
     */
    struct DepotManifest
    {
        std::string depotId;    ///< e.g. "293781"
        std::string manifestGid; ///< e.g. "9207527406397102173"
    };

    /**
     * Parse all setManifestid(<depotId>, "<manifestGid>") entries from
     * `luaContent` and return them in declaration order.
     *
     * Handles optional whitespace and an optional third size argument:
     *   setManifestid( 293781 , "9207527406397102173" )
     *   setManifestid( 293781 , "9207527406397102173" , 1234567 )
     */
    std::vector<DepotManifest> parseManifestPairs(const std::string& luaContent);

    /**
     * Return true if `luaContent` contains at least one addappid() call —
     * the bare minimum for a well-formed Lua plugin.
     */
    bool validateLua(const std::string& luaContent);

    /**
     * Result of collectManifestFiles().
     */
    struct CollectResult
    {
        std::vector<std::string> manifestFiles; ///< Absolute paths to resolved manifests
        bool                     ok;            ///< false if no manifests could be resolved at all
    };

    /**
     * For each (depotId, manifestGid) pair:
     *   1. Check `depotcacheDir` for  <depotId>_<manifestGid>.manifest
     *   2. For any missing: query https://manifest.steam.run/api/depot/<appId>
     *      and, if a manifest GID is returned for that depot, create an empty
     *      placeholder file in `placeholderDir`.
     *
     * `placeholderDir` is created automatically if it does not exist.
     *
     * Returns CollectResult::ok == false only when the final list is empty
     * (all pairs missing AND the API could not fill any of them).
     */
    CollectResult collectManifestFiles(const std::string& appId,
                                       const std::vector<DepotManifest>& pairs,
                                       const std::string& depotcacheDir,
                                       const std::string& placeholderDir);

} // namespace CppAccela::Manifest
