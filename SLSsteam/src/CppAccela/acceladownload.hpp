#pragma once

#include "../sdk/IClientAppManager.hpp"

#include <cstdint>
#include <string>
#include <sys/types.h>

/**
 * CppAccela::Download
 *
 * Owns the full native "Install" flow for apps that are not locally owned.
 * No Python app, no shell script, no ZIP — everything runs in-process:
 *
 *   launchForApp()       — forks a child and enqueues a PendingTask.
 *
 *   runInChild()         — [[noreturn]], runs the complete pipeline:
 *                           1. Parse Lua plugin from stplug-in/
 *                           2. Collect manifest files (depotcache + API fallback)
 *                           3. Run DepotDownloaderMod via dotnet per depot
 *                           4. Post-process: ACF, depotcache, chmod, config.yaml
 *
 *   pollPendingInstalls() — called every IPC frame; when a child exits 0,
 *                           calls originalInstallApp + Apps::setInstalled.
 *
 * Queue capacity: 16 simultaneous downloads.
 */
namespace CppAccela::Download
{
    /**
     * Describes a queued accela-download child process.
     * Mirrors the old PendingInstallTask struct in hooks.cpp.
     */
    struct PendingTask
    {
        IClientAppManager* pClientAppManager;
        uint32_t           appId;
        uint32_t           library;
        uint8_t            a4;
        pid_t              pid;
        bool               paused = false;
    };

    static constexpr int kQueueCapacity = 16;

    /**
     * Run the full ACCELA download pipeline for `appId` and exec a terminal
     * running ACCELA.  This function is designed to run inside a forked child
     * process — it will _exit() on any fatal error and never returns normally.
     *
     * Steps:
     *   1. Find Steam root / stplug-in / depotcache directories.
     *   2. Read and validate <appId>.lua from stplug-in/.
     *   3. Parse setManifestid() pairs; collect manifest files (local + API).
     *   4. Assemble a ZIP in a temp directory.
     *   5. Locate ACCELA run.sh.
     *   6. Open a terminal emulator running: run.sh -cli <zipPath>
     */
    [[noreturn]] void runInChild(uint32_t appId);

    /**
     * Fork a child that runs runInChild(appId) and push the resulting
     * PendingTask onto the queue.  Called from hkClientAppManager_InstallApp.
     *
     * Returns true if the fork succeeded and the task was enqueued.
     */
    bool launchForApp(IClientAppManager* pClientAppManager,
                      uint32_t appId,
                      uint32_t library,
                      uint8_t  a4,
                      const std::string& selectedDepots = "");

    /**
     * Cache an install request so it can be started later via startCachedInstall.
     * This is used to defer the download until the user selects depots in the injected UI.
     */
    void cacheInstallRequest(IClientAppManager* pClientAppManager,
                             uint32_t appId,
                             uint32_t library,
                             uint8_t a4);

    /**
     * Start an install request that was previously cached, passing the user's selected depots.
     * Removes the request from the cache.
     * Returns false if the request was not found in the cache.
     */
    bool startCachedInstall(uint32_t appId, const std::string& selectedDepots);

    /**
     * Discard a cached install request (e.g., if the user cancels the depot selection).
     */
    void discardCachedInstall(uint32_t appId);

    /**
     * Poll every queued child with WNOHANG.  For children that have exited:
     *   - exit 0 or 13 → success: call originalInstallApp + Apps::setInstalled
     *   - other         → failure: log and discard
     *
     * `originalInstallApp` is the trampoline pointer from Hooks::IClientAppManager_InstallApp.
     * Must be called from hkCSteamEngine_ProcessIPCFrame (i.e. every IPC frame).
     */
    using InstallAppFn = uint32_t(*)(IClientAppManager*, uint32_t, uint32_t, uint8_t);
    void pollPendingInstalls(InstallAppFn originalInstallApp);

    /** Current number of items in the pending queue (for debug/logging). */
    int pendingCount();

    /** Returns true if appId has an active accela-helper child process. */
    bool isPending(uint32_t appId);

    /**
     * Send SIGTERM to the accela-helper child for appId.
     * The child will be reaped by the next pollPendingInstalls() call.
     * Returns false if appId is not in the pending queue.
     */
    bool cancelForApp(uint32_t appId);

    /** Suspend the download with SIGSTOP. Returns false if not downloading or already paused. */
    bool pauseForApp(uint32_t appId);

    /** Resume a suspended download with SIGCONT. Returns false if not paused. */
    bool resumeForApp(uint32_t appId);

    /** Returns true if appId is currently paused (SIGSTOP sent, not yet resumed). */
    bool isPaused(uint32_t appId);

} // namespace CppAccela::Download
