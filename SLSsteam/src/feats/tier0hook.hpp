#pragma once

#include <string>

/**
 * Tier0 Hook Module
 * 
 * Hooks libtier0_s.so!CreateSimpleProcess to intercept the steamwebhelper
 * launch and inject --remote-debugging-port=8080 flags.
 * 
 * This replaces the need for --cef-enable-debugging on the main Steam process,
 * eliminating the performance overhead of the full CEF debugging mode.
 * The debug port is only opened on steamwebhelper, not the main Steam process.
 * 
 * On Linux, we use --remote-debugging-port instead of --remote-debugging-pipe
 * because CreateSimpleProcess doesn't give us control over child fd inheritance
 * (pipes require fd 3/4 to be set up before exec).
 * 
 * Technique inspired by the Millennium project:
 * https://github.com/SteamClientHomebrew/Millennium
 */
namespace Tier0Hook
{
    /**
     * Install the CreateSimpleProcess hook on libtier0_s.so.
     * Must be called after libtier0_s.so is loaded (from la_objopen or load()).
     * Returns true if hook was installed successfully.
     */
    bool install();

    /**
     * Remove the CreateSimpleProcess hook.
     * Called during shutdown.
     */
    void remove();

    /**
     * Returns true if the hook was successfully installed and
     * steamwebhelper will have the debug port injected.
     */
    bool isHookInstalled();
}
