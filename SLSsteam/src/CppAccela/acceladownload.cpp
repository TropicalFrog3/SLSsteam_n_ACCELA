#include "acceladownload.hpp"

// CppAccela modules — full native pipeline
#include "accelapath.hpp"
#include "accelaluaparser.hpp"
#include "../atomic_file.hpp"

#include "../feats/apps.hpp"
#include "../config.hpp"
#include "../sdk/IClientApps.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <mutex>
#include <filesystem>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

// Include LOG_* for parent-side functions (launchForApp, pollPendingInstalls)
#include "../log.hpp"

namespace fs = std::filesystem;

namespace CppAccela::Download
{
    // ── Pending queue ─────────────────────────────────────────────────────────

    static PendingTask s_queue[kQueueCapacity];
    static int         s_count = 0;

    // ── Cached requests (pending UI selection) ────────────────────────────────

    struct PendingInstallRequest
    {
        IClientAppManager* pClientAppManager;
        uint32_t library;
        uint8_t a4;
    };

    static std::map<uint32_t, PendingInstallRequest> s_cachedRequests;
    static std::mutex s_cachedRequestsMutex;

    // ── Path to accela-helper binary ──────────────────────────────────────────

    static std::string getHelperPath()
    {
        // The helper binary lives alongside the SLSsteam.so library.
        
        // Try environment variable first (set by setup.sh)
        const char* envPath = getenv("SLSSTEAM_HELPER_PATH");
        if (envPath && envPath[0])
            return envPath;

        // Try ~/.local/share/SLSsteam/accela-helper (standard install location)
        const char* home = getenv("HOME");
        if (home)
        {
            std::string path = std::string(home) + "/.local/share/SLSsteam/accela-helper";
            if (access(path.c_str(), X_OK) == 0)
                return path;
        }

        // Try /usr/local/bin/accela-helper
        if (access("/usr/local/bin/accela-helper", X_OK) == 0)
            return "/usr/local/bin/accela-helper";

        // Try /usr/bin/accela-helper
        if (access("/usr/bin/accela-helper", X_OK) == 0)
            return "/usr/bin/accela-helper";

        return "";
    }

    // ── Progress JSON writer (parent-side) ────────────────────────────────────
    // Writes the initial "starting" JSON *before* forking the child so /check
    // can immediately return downloading:true without waiting for the child to
    // boot, parse Lua, and write its own first progress entry (Fix 4).

    static void writeStartingJson(uint32_t appId)
    {
        const char* tmpDir = getenv("TMPDIR");
        const std::string path =
            std::string(tmpDir ? tmpDir : "/tmp")
            + "/sls_dl_" + std::to_string(appId) + ".json";

        const char json[] =
            "{\"phase\":\"starting\",\"gameName\":\"\","
            "\"depotsDone\":0,\"depotsTotal\":0,\"percent\":0}\n";

        const std::string tmp = path + ".tmp";
        FILE* f = fopen(tmp.c_str(), "w");
        if (!f) return;
        fputs(json, f);
        fclose(f);
        rename(tmp.c_str(), path.c_str());
    }

    // ── Orphan process cleanup ────────────────────────────────────────────────
    void terminateOrphanProcesses(uint32_t appId, const std::string& downloadDir)
    {
        const std::string appIdStr = std::to_string(appId);
        const pid_t myPid = getpid();
        std::vector<pid_t> toKill;

        // 1. Check PID file if present
        const char* tmpDir = getenv("TMPDIR");
        std::string pidFilePath = std::string(tmpDir ? tmpDir : "/tmp")
                                  + "/sls_dl_" + appIdStr + ".pid";
        {
            FILE* pf = fopen(pidFilePath.c_str(), "r");
            if (pf)
            {
                pid_t savedPid = 0;
                if (fscanf(pf, "%d", &savedPid) == 1 && savedPid > 1 && savedPid != myPid)
                {
                    toKill.push_back(savedPid);
                }
                fclose(pf);
                unlink(pidFilePath.c_str());
            }
        }

        // 2. Scan /proc for any accela-helper or DepotDownloader matching appId or downloadDir
        DIR* procDir = opendir("/proc");
        if (procDir)
        {
            struct dirent* ent;
            while ((ent = readdir(procDir)) != nullptr)
            {
                if (ent->d_type != DT_DIR && ent->d_type != DT_UNKNOWN) continue;
                char* endptr = nullptr;
                long pidL = strtol(ent->d_name, &endptr, 10);
                if (*endptr != '\0' || pidL <= 1 || pidL == myPid) continue;
                pid_t pid = static_cast<pid_t>(pidL);

                std::string cmdlinePath = std::string("/proc/") + ent->d_name + "/cmdline";
                int fd = open(cmdlinePath.c_str(), O_RDONLY);
                if (fd < 0) continue;

                char buf[2048];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);
                close(fd);
                if (n <= 0) continue;
                buf[n] = '\0';

                // cmdline arguments are null-separated; convert to space-separated for searching
                for (ssize_t i = 0; i < n; ++i)
                {
                    if (buf[i] == '\0') buf[i] = ' ';
                }
                std::string cmd(buf, n);

                bool isAccelaHelper = (cmd.find("accela-helper") != std::string::npos &&
                                       cmd.find(appIdStr) != std::string::npos);
                bool isDepotDownloader = (cmd.find("DepotDownloaderMod") != std::string::npos ||
                                          cmd.find("DepotDownloader") != std::string::npos) &&
                                         (cmd.find(appIdStr) != std::string::npos ||
                                          (!downloadDir.empty() && cmd.find(downloadDir) != std::string::npos));

                if (isAccelaHelper || isDepotDownloader)
                {
                    toKill.push_back(pid);
                }
            }
            closedir(procDir);
        }

        // 3. Terminate all matched processes
        for (pid_t pid : toKill)
        {
            if (kill(pid, 0) == 0) // check if still alive
            {
                LOG_INFO("AccelaDownload: terminating orphan process %d for appid=%u\n", pid, appId);
                pid_t pgid = getpgid(pid);
                if (pgid > 1)
                {
                    kill(-pgid, SIGCONT);
                    kill(-pgid, SIGKILL);
                }
                kill(pid, SIGCONT);
                kill(pid, SIGKILL);
            }
        }

        // 4. Wait for them to disappear so file locks are released
        if (!toKill.empty())
        {
            for (int i = 0; i < 30; ++i)
            {
                bool anyAlive = false;
                for (pid_t pid : toKill)
                {
                    if (kill(pid, 0) == 0) { anyAlive = true; break; }
                }
                if (!anyAlive) break;
                usleep(10000); // 10ms (max 300ms)
            }
        }
    }

    // ── Parent-side: fork + exec ───────────────────────────────────────────────
    // LOG_* macros are safe here — we are still in the Steam process.

    bool launchForApp(IClientAppManager* pClientAppManager,
                      uint32_t appId,
                      uint32_t library,
                      uint8_t  a4,
                      const std::string& selectedDepots)
    {
        if (s_count >= kQueueCapacity)
        {
            LOG_WARN("AccelaDownload: pending install queue full, dropping appid=%u\n",
                     appId);
            return false;
        }

        if (isPending(appId))
        {
            LOG_WARN("AccelaDownload: appid=%u is already in queue, cancelling previous download\n", appId);
            cancelForApp(appId);
        }

        const std::string helperPath = getHelperPath();
        if (helperPath.empty())
        {
            LOG_WARN("AccelaDownload: accela-helper binary not found\n");
            return false;
        }

        // Fetch the official installdir from Steam's memory if available (MUST DO BEFORE FORK)
        char installDirBuf[512] = {0};
        if (g_pClientApps)
        {
            g_pClientApps->getAppData(appId, "config/installdir", installDirBuf, sizeof(installDirBuf));
        }

        // Clean up any orphaned processes and lingering file locks from previous sessions/crashes
        terminateOrphanProcesses(appId, installDirBuf);

        // If a stale appmanifest was left behind from a previous failed run, remove it
        std::string steamRoot = CppAccela::Path::findSteamRoot();
        if (!steamRoot.empty())
        {
            std::string staleAcf = steamRoot + "/steamapps/appmanifest_" + std::to_string(appId) + ".acf";
            std::error_code ec;
            if (fs::exists(staleAcf, ec))
            {
                LOG_INFO("AccelaDownload: removing stale manifest from previous failed attempt: %s\n", staleAcf.c_str());
                fs::remove(staleAcf, ec);
            }

            // Clean up any stale staging folder from an earlier aborted attempt
            std::string staleDownloading = steamRoot + "/steamapps/downloading/" + std::to_string(appId);
            AtomicFile::removePath(staleDownloading);
        }

        // Fix 4: write "starting" JSON from the parent *before* forking.
        // This lets /check immediately see downloading:true (via the progress-file
        // check in storeinject.cpp) without waiting for the child to spin up,
        // eliminating the 15-second+ feedback delay.
        writeStartingJson(appId);

        const std::string appIdStr = std::to_string(appId);
        const pid_t parentPid = getpid();

        pid_t pid = fork();
        if (pid < 0)
        {
            LOG_WARN("AccelaDownload: fork() failed for appid=%u\n", appId);
            return false;
        }

        if (pid == 0)
        {
            // ── Child ──────────────────────────────────────────────────────
            // After fork(), exec() immediately to get a clean process.
            // This avoids all multithreading deadlocks from Steam's threads.

            setsid();
            // Use SIGKILL so child terminates immediately on parent death even if suspended/paused!
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parentPid) _exit(1);

            if (installDirBuf[0] != '\0')
            {
                setenv("ACCELA_INSTALLDIR", installDirBuf, 1);
            }

            if (!selectedDepots.empty())
            {
                setenv("ACCELA_SELECTED_DEPOTS", selectedDepots.c_str(), 1);
            }

            // Close all inherited Steam FDs
            for (int fd = 3; fd < 256; fd++) close(fd);

            // Strip Steam runtime injections
            unsetenv("LD_AUDIT");
            unsetenv("LD_PRELOAD");
            unsetenv("LD_LIBRARY_PATH");
            unsetenv("STEAM_RUNTIME");
            unsetenv("STEAM_RUNTIME_LIBRARY_PATH");

            // Build argv for exec
            const char* argv[] = {
                helperPath.c_str(),
                appIdStr.c_str(),
                nullptr
            };

            execv(helperPath.c_str(), const_cast<char* const*>(argv));

            // If exec fails, we must not call any non-async-signal-safe
            // functions. Use _exit() immediately.
            const char msg[] = "AccelaDownload: execv failed\n";
            write(2, msg, sizeof(msg) - 1);
            _exit(127);
        }

        // ── Parent ─────────────────────────────────────────────────────────
        LOG_INFO("AccelaDownload: tracking child PID %d for appid=%u\n",
                 pid, appId);
        s_queue[s_count++] = { pClientAppManager, appId, library, a4, pid };
        return true;
    }

    // ── Cancel a pending download ─────────────────────────────────────────────
    // Sends SIGTERM to the accela-helper child and its process group.
    // The child will be reaped by the next pollPendingInstalls() call.

    bool cancelForApp(uint32_t appId)
    {
        for (int i = 0; i < s_count; ++i)
        {
            if (s_queue[i].appId != appId) continue;

            const pid_t pid = s_queue[i].pid;
            LOG_INFO("AccelaDownload: cancelling appid=%u (PID %d)\n", appId, pid);

            // Wake up if paused (SIGSTOP) so signal can be processed, then send SIGTERM
            kill(-pid, SIGCONT);
            kill(pid,  SIGCONT);
            kill(-pid, SIGTERM);
            kill(pid,  SIGTERM);

            // Wait up to 2 s for the child to exit so its file handles are
            // closed before we delete the partial files. Use WNOHANG polling
            // so we don't block Steam's IPC thread indefinitely.
            bool exited = false;
            for (int attempt = 0; attempt < 20; ++attempt)
            {
                int status = 0;
                if (waitpid(pid, &status, WNOHANG) > 0) { exited = true; break; }
                usleep(100000); // 100 ms
            }

            if (!exited)
            {
                // Force kill if it didn't exit
                kill(-pid, SIGKILL);
                kill(pid,  SIGKILL);
                waitpid(pid, nullptr, WNOHANG);
            }

            // Also clean up any lingering orphan processes for this app
            terminateOrphanProcesses(appId, "");

            // Update progress JSON to "cancelled" phase immediately so the UI
            // card changes state before the cleanup even starts.
            const char* tmpDir = getenv("TMPDIR");
            const std::string progressPath =
                std::string(tmpDir ? tmpDir : "/tmp")
                + "/sls_dl_" + std::to_string(appId) + ".json";
            const char cancelledJson[] =
                "{\"phase\":\"failed\",\"gameName\":\"\","
                "\"depotsDone\":0,\"depotsTotal\":0,\"percent\":0,"
                "\"speedBps\":0,\"etaSec\":-1}\n";
            const std::string progressTmp = progressPath + ".tmp";
            FILE* pf = fopen(progressTmp.c_str(), "w");
            if (pf) { fputs(cancelledJson, pf); fclose(pf); rename(progressTmp.c_str(), progressPath.c_str()); }

            // Clean up the temporary downloading directory.
            // Crucially: do NOT call Apps::deleteGameFiles(appId) or touch steamapps/common/
            // so that any existing installed game files are preserved without corruption!
            std::string steamRoot = CppAccela::Path::findSteamRoot();
            if (!steamRoot.empty())
            {
                std::string downloadingDir = steamRoot + "/steamapps/downloading/" + std::to_string(appId);
                LOG_INFO("AccelaDownload: cancel cleaning up temporary download directory %s for appid=%u\n",
                         downloadingDir.c_str(), appId);
                AtomicFile::removePath(downloadingDir);
            }

            // Cleanup global temp files that accela-helper may leave behind on SIGTERM
            std::error_code ec;
            std::string tmpBase = tmpDir ? tmpDir : "/tmp";
            std::filesystem::remove_all(tmpBase + "/mistwalker_manifests", ec);
            std::filesystem::remove_all(tmpBase + "/accela_placeholders", ec);
            std::filesystem::remove(tmpBase + "/mistwalker_keys.vdf", ec);

            // Remove from the pending queue
            s_queue[i] = s_queue[--s_count];

            return true;
        }
        LOG_WARN("AccelaDownload: cancelForApp(%u) — not in pending queue\n", appId);
        return false;
    }


    // ── Pause / resume a pending download ────────────────────────────────────
    // SIGSTOP suspends the entire process group (accela-helper + dotnet).
    // SIGCONT resumes it. The progress JSON phase is patched so the UI shows
    // "Paused" and can swap the button accordingly.

    bool pauseForApp(uint32_t appId)
    {
        for (int i = 0; i < s_count; ++i)
        {
            if (s_queue[i].appId != appId) continue;
            if (s_queue[i].paused)
            {
                LOG_WARN("AccelaDownload: pauseForApp(%u) already paused\n", appId);
                return false;
            }
            const pid_t pid = s_queue[i].pid;
            LOG_INFO("AccelaDownload: pausing appid=%u (PID %d)\n", appId, pid);
            kill(-pid, SIGSTOP);
            kill(pid,  SIGSTOP);
            s_queue[i].paused = true;

            const char* tmpDir = getenv("TMPDIR");
            std::string path = std::string(tmpDir ? tmpDir : "/tmp")
                               + "/sls_dl_" + std::to_string(appId) + ".json";
            std::string existing;
            { FILE* rf = fopen(path.c_str(), "r");
              if (rf) { char rbuf[1024]; size_t nr = fread(rbuf, 1, sizeof(rbuf)-1, rf);
                        fclose(rf); rbuf[nr] = '\0'; existing = rbuf; } }
            if (!existing.empty()) {
                size_t p = existing.find("\"phase\":\"");
                if (p != std::string::npos) {
                    size_t v = p + 9, e = existing.find('\"', v);
                    if (e != std::string::npos) existing.replace(v, e - v, "paused");
                }
            } else {
                existing = "{\"phase\":\"paused\",\"gameName\":\"\","
                           "\"depotsDone\":0,\"depotsTotal\":0,\"percent\":0,"
                           "\"speedBps\":0,\"etaSec\":-1}\n";
            }
            std::string tmp = path + ".tmp";
            FILE* wf = fopen(tmp.c_str(), "w");
            if (wf) { fputs(existing.c_str(), wf); fclose(wf); rename(tmp.c_str(), path.c_str()); }
            return true;
        }
        LOG_WARN("AccelaDownload: pauseForApp(%u) not in queue\n", appId);
        return false;
    }

    bool resumeForApp(uint32_t appId)
    {
        for (int i = 0; i < s_count; ++i)
        {
            if (s_queue[i].appId != appId) continue;
            if (!s_queue[i].paused)
            {
                LOG_WARN("AccelaDownload: resumeForApp(%u) not paused\n", appId);
                return false;
            }
            const pid_t pid = s_queue[i].pid;
            LOG_INFO("AccelaDownload: resuming appid=%u (PID %d)\n", appId, pid);
            kill(-pid, SIGCONT);
            kill(pid,  SIGCONT);
            s_queue[i].paused = false;

            const char* tmpDir = getenv("TMPDIR");
            std::string path = std::string(tmpDir ? tmpDir : "/tmp")
                               + "/sls_dl_" + std::to_string(appId) + ".json";
            std::string existing;
            { FILE* rf = fopen(path.c_str(), "r");
              if (rf) { char rbuf[1024]; size_t nr = fread(rbuf, 1, sizeof(rbuf)-1, rf);
                        fclose(rf); rbuf[nr] = '\0'; existing = rbuf; } }
            if (!existing.empty()) {
                size_t p = existing.find("\"phase\":\"");
                if (p != std::string::npos) {
                    size_t v = p + 9, e = existing.find('\"', v);
                    if (e != std::string::npos) existing.replace(v, e - v, "downloading");
                }
            }
            std::string tmp = path + ".tmp";
            FILE* wf = fopen(tmp.c_str(), "w");
            if (wf) { fputs(existing.c_str(), wf); fclose(wf); rename(tmp.c_str(), path.c_str()); }
            return true;
        }
        LOG_WARN("AccelaDownload: resumeForApp(%u) not in queue\n", appId);
        return false;
    }

    bool isPaused(uint32_t appId)
    {
        for (int i = 0; i < s_count; ++i)
            if (s_queue[i].appId == appId) return s_queue[i].paused;
        return false;
    }

    void pollPendingInstalls(InstallAppFn originalInstallApp)
    {
        for (int i = 0; i < s_count; )
        {
            int   status = 0;
            pid_t res    = waitpid(s_queue[i].pid, &status, WNOHANG);

            if (res > 0)
            {
                const uint32_t pendingAppId = s_queue[i].appId;
                LOG_INFO("AccelaDownload: child PID %d exited for appid=%u "
                         "(raw status %d)\n", res, pendingAppId, status);

                bool success = false;
                if (WIFEXITED(status))
                    success = (WEXITSTATUS(status) == 0);

                if (success)
                {
                    LOG_INFO("AccelaDownload: download succeeded for appid=%u, "
                             "finalising Steam installation\n", pendingAppId);

                    if (originalInstallApp)
                        originalInstallApp(s_queue[i].pClientAppManager,
                                           pendingAppId,
                                           s_queue[i].library,
                                           s_queue[i].a4);

                    Apps::setInstalled(pendingAppId);

                    // Fix 5: force Steam UI to recognise the game as installed
                    // without requiring a restart. The GetAppInstallState hook
                    // already forces FULLY_INSTALLED on every query, but the UI
                    // may not re-poll until something triggers a refresh.
                    // Pushing to g_config.newApps causes runIPCFrame() to call
                    // requestAppInfoUpdate() + AppLicensesChanged callback,
                    // which tells the Steam client to re-evaluate the app's state.
                    {
                        const std::lock_guard<std::mutex> lock(g_config.appsChangedMutex);
                        g_config.newApps.emplace(pendingAppId);
                    }
                }
                else
                {
                    LOG_WARN("AccelaDownload: download failed or cancelled "
                             "for appid=%u (exit %d)\n",
                             pendingAppId,
                             WIFEXITED(status) ? WEXITSTATUS(status) : -1);

                    std::string steamRoot = CppAccela::Path::findSteamRoot();
                    if (!steamRoot.empty())
                    {
                        std::string downloadingDir = steamRoot + "/steamapps/downloading/" + std::to_string(pendingAppId);
                        AtomicFile::removePath(downloadingDir);
                    }
                }

                // Swap-with-last O(1) removal
                s_queue[i] = s_queue[--s_count];
            }
            else if (res < 0)
            {
                LOG_WARN("AccelaDownload: waitpid failed for PID %d (appid=%u) with errno=%d\n", s_queue[i].pid, s_queue[i].appId, errno);
                s_queue[i] = s_queue[--s_count];
            }
            else
            {
                ++i;
            }
        }
    }

    int pendingCount()
    {
        return s_count;
    }

    bool isPending(uint32_t appId)
    {
        for (int i = 0; i < s_count; ++i)
            if (s_queue[i].appId == appId) return true;
        return false;
    }

    std::vector<uint32_t> getPendingAppIds()
    {
        std::vector<uint32_t> ids;
        for (int i = 0; i < s_count; ++i)
            ids.push_back(s_queue[i].appId);
        return ids;
    }

    void cacheInstallRequest(IClientAppManager* pClientAppManager, uint32_t appId, uint32_t library, uint8_t a4)
    {
        std::lock_guard<std::mutex> lock(s_cachedRequestsMutex);
        s_cachedRequests[appId] = { pClientAppManager, library, a4 };
        LOG_INFO("AccelaDownload: cached install request for appid=%u\n", appId);
    }

    bool startCachedInstall(uint32_t appId, const std::string& selectedDepots)
    {
        IClientAppManager* pClientAppManager = nullptr;
        uint32_t library = 0;
        uint8_t a4 = 0;

        {
            std::lock_guard<std::mutex> lock(s_cachedRequestsMutex);
            auto it = s_cachedRequests.find(appId);
            if (it == s_cachedRequests.end())
            {
                LOG_WARN("AccelaDownload: startCachedInstall failed, no request cached for appid=%u\n", appId);
                return false;
            }
            pClientAppManager = it->second.pClientAppManager;
            library = it->second.library;
            a4 = it->second.a4;
            s_cachedRequests.erase(it);
        }

        LOG_INFO("AccelaDownload: starting cached install for appid=%u with depots='%s'\n", appId, selectedDepots.c_str());
        return launchForApp(pClientAppManager, appId, library, a4, selectedDepots);
    }

    void discardCachedInstall(uint32_t appId)
    {
        std::lock_guard<std::mutex> lock(s_cachedRequestsMutex);
        if (s_cachedRequests.erase(appId))
        {
            LOG_INFO("AccelaDownload: discarded cached install request for appid=%u\n", appId);
        }
    }

    void shutdown()
    {
        LOG_INFO("AccelaDownload: shutting down, terminating %d active download(s)\n", s_count);
        std::string steamRoot = CppAccela::Path::findSteamRoot();
        for (int i = 0; i < s_count; ++i)
        {
            const pid_t pid = s_queue[i].pid;
            const uint32_t appId = s_queue[i].appId;
            kill(-pid, SIGCONT);
            kill(pid,  SIGCONT);
            kill(-pid, SIGKILL);
            kill(pid,  SIGKILL);

            if (!steamRoot.empty())
            {
                std::string downloadingDir = steamRoot + "/steamapps/downloading/" + std::to_string(appId);
                LOG_INFO("AccelaDownload: shutdown cleaning up temporary download directory %s\n", downloadingDir.c_str());
                AtomicFile::removePath(downloadingDir);
            }
        }
        s_count = 0;
    }

    void purgeStaleDownloads()
    {
        const std::string steamRoot = CppAccela::Path::findSteamRoot();
        if (steamRoot.empty()) return;

        const std::string downloadingPath = steamRoot + "/steamapps/downloading";
        std::error_code ec;
        if (!fs::exists(downloadingPath, ec) || !fs::is_directory(downloadingPath, ec))
            return;

        const char* tmpDir = getenv("TMPDIR");
        const std::string tmpBase = tmpDir ? tmpDir : "/tmp";
        const std::string stplugDir = CppAccela::Path::stplugDir();

        for (const auto& entry : fs::directory_iterator(downloadingPath, ec))
        {
            if (ec) break;
            if (entry.is_directory(ec))
            {
                std::string folderName = entry.path().filename().string();
                bool isNumeric = !folderName.empty() && std::all_of(folderName.begin(), folderName.end(), ::isdigit);
                if (isNumeric)
                {
                    uint32_t appId = 0;
                    try { appId = std::stoul(folderName); } catch (...) { continue; }

                    if (!isPending(appId))
                    {
                        bool isSlsApp = false;
                        if (!stplugDir.empty() && fs::exists(stplugDir + "/" + folderName + ".lua", ec))
                        {
                            isSlsApp = true;
                        }
                        else if (fs::exists(tmpBase + "/sls_dl_" + folderName + ".json", ec) ||
                                 fs::exists(tmpBase + "/sls_dl_" + folderName + ".pid", ec))
                        {
                            isSlsApp = true;
                        }

                        if (isSlsApp)
                        {
                            terminateOrphanProcesses(appId, "");
                            LOG_INFO("AccelaDownload: purging stale leftover download directory: %s\n",
                                     entry.path().string().c_str());
                            AtomicFile::removePath(entry.path().string());
                        }
                    }
                }
            }
        }
    }

} // namespace CppAccela::Download
