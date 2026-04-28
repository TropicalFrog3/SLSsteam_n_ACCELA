#pragma once

#include <cstdint>

namespace CefSizeFix
{
    /**
     * Inject the SLS size-fix JavaScript into Steam's steamui/index.html.
     * This patches SteamClient.Apps.RegisterForAppDetails in the JS layer
     * to replace lDiskSpaceRequiredBytes=0 with real sizes fetched from
     * the steamcmd.net API for SLS-unlocked games.
     *
     * Called once during SLSsteam initialization (from main.cpp load()).
     * The injection is idempotent — re-calling it will not double-inject.
     *
     * @return true if injection succeeded or was already present.
     */
    bool injectSizeFixScript();

    /**
     * Remove the injected script tag from index.html (cleanup on unload).
     * Restores the original file content.
     */
    void removeSizeFixScript();
}
