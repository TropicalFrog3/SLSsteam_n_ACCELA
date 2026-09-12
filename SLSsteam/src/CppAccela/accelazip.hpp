#pragma once

#include <string>
#include <vector>

/**
 * CppAccela::Zip
 *
 * ZIP file helpers used by the ACCELA workflow:
 *   - Validation  : check PK magic bytes before doing anything with a file
 *   - Assembly    : pack a Lua plugin + manifest files into a flat ZIP
 *                   (mirrors accela-download.sh's  zip -j -0  step)
 *   - Extraction  : unpack a ZIP to a directory (used by LuaDownload)
 *
 * All operations use fork/exec to avoid linking an extra library and to
 * stay consistent with the rest of the codebase.
 */
namespace CppAccela::Zip
{
    /**
     * Return true if `path` begins with a valid PK magic signature.
     * Accepts normal (PK\x03\x04), empty (PK\x05\x06), and spanned
     * (PK\x07\x08) ZIPs — the same set that Python's zipfile accepts.
     */
    bool isValid(const std::string& path);

    /**
     * Assemble a flat ZIP (no compression, no path stripping) at `zipPath`
     * containing `luaPath` and every file listed in `manifestPaths`.
     *
     * Equivalent to:
     *   zip -j -0 <zipPath> <luaPath> <manifestPaths...>
     *
     * Returns true on success.
     * On failure the partially-written zip is removed.
     */
    bool assemble(const std::string& zipPath,
                  const std::string& luaPath,
                  const std::vector<std::string>& manifestPaths);

    /**
     * Extract `zipPath` into `destDir` (created if absent).
     *
     * Equivalent to:
     *   unzip -o -q <zipPath> -d <destDir>
     *
     * Returns true on success.
     */
    bool extract(const std::string& zipPath, const std::string& destDir);

    /**
     * Files found after extracting a Lua-plugin ZIP.
     */
    struct ExtractedFiles
    {
        std::string              luaFile;       ///< Full path to <appid>.lua
        std::vector<std::string> manifestFiles; ///< Full paths to every *.manifest
    };

    /**
     * Recursively walk `extractDir` and collect the file matching
     * `<appId>.lua` and every `*.manifest` file found.
     */
    ExtractedFiles findFiles(const std::string& extractDir,
                             const std::string& appId);

} // namespace CppAccela::Zip
