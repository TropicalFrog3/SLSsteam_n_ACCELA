#pragma once

namespace RemoveLua
{
    /**
     * Inject the JavaScript that adds the "Remove Lua" button
     * next to the "Manage" button in the Steam Library.
     */
    bool injectRemoveLuaScript();

    /**
     * Remove the injected script from steamui/index.html.
     */
    void removeRemoveLuaScript();
}
