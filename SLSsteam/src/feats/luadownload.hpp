#pragma once

#include <string>

namespace LuaDownload
{
    /**
     * Download and install a Lua script + manifest files for the given Steam app ID.
     *
     * Flow:
     *   1. Iterate free API providers until one returns a valid .zip
     *   2. Validate the zip (PK magic bytes)
     *   3. Extract with unzip to a temp directory
     *   4. Copy .manifest files to {steam_root}/depotcache/
     *   5. Process and install {appid}.lua to {steam_root}/config/stplug-in/
     *
     * This runs synchronously — call from a detached thread.
     * Returns true if the download and install succeeded.
     */
    bool downloadAndInstall(const std::string& appId);
}
