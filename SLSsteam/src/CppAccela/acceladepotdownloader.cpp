#include "acceladepotdownloader.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>

// Safe logging for standalone helper — no g_pLog dependency
#define DD_LOG(fmt, ...) fprintf(stderr, "[DepotDownloader] " fmt __VA_OPT__(,) __VA_ARGS__)

namespace fs = std::filesystem;

namespace CppAccela::DepotDownloader
{
    // ── Filesystem sanitisation ───────────────────────────────────────────────

    /**
     * Produce a filesystem-safe directory name from a game title:
     * strips characters outside  [A-Za-z0-9_\s-], collapses whitespace,
     * replaces spaces with underscores.
     * Mirrors Python:  re.sub(r"[^\w\s-]", "", name).strip().replace(" ", "_")
     */
    static std::string sanitiseName(const std::string& name)
    {
        std::string s;
        s.reserve(name.size());
        for (unsigned char c : name)
        {
            if (std::isalnum(c) || c == '_' || c == '-' || c == ' ')
                s.push_back(c);
        }
        // Trim leading/trailing spaces
        size_t start = s.find_first_not_of(' ');
        if (start == std::string::npos) return {};
        size_t end = s.find_last_not_of(' ');
        s = s.substr(start, end - start + 1);
        // Do not replace spaces with underscores to match Steam's directory naming exactly.
        // std::replace(s.begin(), s.end(), ' ', '_');
        return s;
    }

    // ── Download directory ────────────────────────────────────────────────────

    std::string findDotnet()
    {
        // 1. $DOTNET_ROOT/dotnet
        if (const char* root = getenv("DOTNET_ROOT"))
        {
            fs::path p = fs::path(root) / "dotnet";
            if (fs::exists(p)) return p.string();
        }

        // 2. $HOME/.dotnet/dotnet
        if (const char* home = getenv("HOME"))
        {
            fs::path p = fs::path(home) / ".dotnet" / "dotnet";
            if (fs::exists(p)) return p.string();
        }

        // 3. Well-known system paths
        for (const char* path : { "/usr/bin/dotnet", "/usr/local/bin/dotnet" })
        {
            if (fs::exists(path)) return path;
        }

        // 4. Walk $PATH
        if (const char* pathEnv = getenv("PATH"))
        {
            std::istringstream ss(pathEnv);
            std::string dir;
            while (std::getline(ss, dir, ':'))
            {
                if (dir.empty()) continue;
                fs::path candidate = fs::path(dir) / "dotnet";
                if (fs::exists(candidate)) return candidate.string();
            }
        }

        return {};
    }

    std::string findDepotDownloaderDll()
    {
        const char* home = getenv("HOME");
        if (!home) return {};

        // 1. SLSsteam bundled location
        fs::path slsDll = fs::path(home)
            / ".local" / "share" / "SLSsteam"
            / "deps" / "DepotDownloaderMod.dll";
        if (fs::exists(slsDll)) return slsDll.string();

        // 2. Installed ACCELA location
        fs::path accelaDll = fs::path(home)
            / ".local" / "share" / "ACCELA"
            / "src" / "deps" / "DepotDownloaderMod.dll";
        if (fs::exists(accelaDll)) return accelaDll.string();

        return {};
    }

    // ── Keys VDF ─────────────────────────────────────────────────────────────

    /**
     * Write /tmp/mistwalker_keys.vdf containing one line per selected depot:
     *   <depotId>;<hexKey>
     *
     * Returns the path written, or empty on failure.
     */
    static std::string writeKeysVdf(
        const LuaParser::ParseResult& lua,
        const std::vector<std::string>& selectedDepots)
    {
        const char* tmpDir = getenv("TMPDIR");
        const std::string keysPath =
            std::string(tmpDir ? tmpDir : "/tmp") + "/mistwalker_keys.vdf";

        std::ofstream f(keysPath, std::ios::trunc);
        if (!f.is_open())
        {
            DD_LOG("cannot write keys VDF at %s\n",
                     keysPath.c_str());
            return {};
        }

        for (const auto& depotId : selectedDepots)
        {
            auto it = lua.depots.find(depotId);
            if (it == lua.depots.end()) continue;
            f << depotId << ";" << it->second.key << "\n";
        }

        DD_LOG("wrote keys VDF (%zu depots) -> %s\n",
                 selectedDepots.size(), keysPath.c_str());
        return keysPath;
    }

    // ── Progress file helper ─────────────────────────────────────────────────

    /**
     * Escape a string for embedding in a JSON string value.
     */
    static std::string jsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    }

    /**
     * Atomically write the progress JSON to progressPath.
     * percent is the actual file-level percentage (0–100).
     * speedBps is download speed in bytes/sec (0 = unknown).
     * etaSec   is estimated seconds remaining (-1 = unknown).
     */
    static void writeProgressJson(const std::string& progressPath,
                                  const std::string& gameName,
                                  int  depotsDone,
                                  int  depotsTotal,
                                  int  percent,
                                  long long speedBps = 0,
                                  int  etaSec = -1)
    {
        if (progressPath.empty()) return;

        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "{\"phase\":\"downloading\",\"gameName\":\"%s\","
                 "\"depotsDone\":%d,\"depotsTotal\":%d,\"percent\":%d,"
                 "\"speedBps\":%lld,\"etaSec\":%d}\n",
                 jsonEscape(gameName).c_str(), depotsDone, depotsTotal, percent,
                 speedBps, etaSec);

        const std::string tmp = progressPath + ".tmp";
        FILE* f = fopen(tmp.c_str(), "w");
        if (!f) return;
        fputs(buf, f);
        fclose(f);
        rename(tmp.c_str(), progressPath.c_str());
    }

    // ── Subprocess runner ─────────────────────────────────────────────────────

    /**
     * Run a command, streaming its combined stdout+stderr line-by-line.
     * Parses DDM progress lines of the form "NN.NN%" and writes live
     * file-level progress to progressPath (when non-empty).
     *
     * depotsDone / depotsTotal are used to scale the per-depot percentage
     * into an overall job percentage:
     *   overall = (depotsDone + thisDepotPct/100) / depotsTotal * 90 + 5
     *
     * Returns the process exit code, or -1 on fork/exec failure.
     */
    static int runAndStream(const std::vector<std::string>& args,
                            const std::string& dotnetRoot,
                            const std::string& progressPath = {},
                            const std::string& gameName     = {},
                            int  depotsDone  = 0,
                            int  depotsTotal = 1,
                            uint64_t totalBytes = 0)
    {
        if (args.empty()) return -1;

        // Build C-style argv
        std::vector<const char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        // Pipe for child stdout+stderr → parent
        int pipefd[2];
        if (pipe(pipefd) != 0)
        {
            DD_LOG("pipe() failed: %s\n", strerror(errno));
            return -1;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            DD_LOG("fork() failed: %s\n", strerror(errno));
            return -1;
        }

        if (pid == 0)
        {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() == 1) _exit(1);

            // Child: redirect stdout + stderr into write-end of pipe
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            // Set DOTNET_ROOT so the runtime finds its own libraries
            if (!dotnetRoot.empty())
                setenv("DOTNET_ROOT", dotnetRoot.c_str(), 1);

            execvp(argv[0], const_cast<char* const*>(argv.data()));
            _exit(127);
        }

        // Parent: read from read-end, log and parse progress line by line
        close(pipefd[1]);

        {
            using Clock = std::chrono::steady_clock;
            using Ms    = std::chrono::milliseconds;

            std::string lineBuf;
            char buf[4096];
            ssize_t n;
            int lastPercent = -1;

            // Speed / ETA tracking state for this depot.
            // We track (percent, timestamp) pairs and compute a rolling
            // bytes-per-second estimate from the last two progress points.
            // Assumes result.totalBytes contains the total download size.
            float  lastPct   = -1.0f;
            auto   lastTime  = Clock::now();
            long long speedBps = 0;
            int      etaSec    = -1;

            while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
            {
                lineBuf.append(buf, static_cast<size_t>(n));
                size_t pos;
                while ((pos = lineBuf.find('\n')) != std::string::npos)
                {
                    std::string line = lineBuf.substr(0, pos);
                    lineBuf.erase(0, pos + 1);
                    if (line.empty()) continue;

                    DD_LOG("DDM| %s\n", line.c_str());

                    // Parse DDM progress: look for "NN.NN%" anywhere in line.
                    // DDM emits lines like: "100.00% (123456789 / 123456789)"
                    // The Python side uses regex r"(\d{1,3}\.\d{2})%"
                    if (!progressPath.empty())
                    {
                        size_t pctPos = line.find('%');
                        if (pctPos != std::string::npos && pctPos >= 2)
                        {
                            // Walk back to find start of the decimal number
                            size_t numEnd = pctPos; // exclusive
                            size_t dotPos = std::string::npos;
                            size_t numStart = pctPos;
                            while (numStart > 0)
                            {
                                char c = line[numStart - 1];
                                if (std::isdigit(c)) { --numStart; }
                                else if (c == '.' && dotPos == std::string::npos)
                                { dotPos = numStart - 1; --numStart; }
                                else break;
                            }
                            if (dotPos != std::string::npos && numStart < numEnd)
                            {
                                std::string numStr = line.substr(numStart, numEnd - numStart);
                                try {
                                    float depotPct = std::stof(numStr);
                                    if (depotPct >= 0.0f && depotPct <= 100.0f)
                                    {
                                        // ── Speed / ETA calculation ──────────────
                                        auto now      = Clock::now();
                                        long long dtMs = std::chrono::duration_cast<Ms>(now - lastTime).count();

                                        if (lastPct >= 0.0f && dtMs >= 500 && totalBytes > 0)
                                        {
                                            // Bytes downloaded since last sample (for this depot)
                                            float depotFraction = static_cast<float>(totalBytes)
                                                                  / static_cast<float>(depotsTotal > 0 ? depotsTotal : 1);
                                            float deltaBytes = (depotPct - lastPct) / 100.0f * depotFraction;
                                            if (deltaBytes > 0.0f && dtMs > 0)
                                            {
                                                speedBps = static_cast<long long>(deltaBytes * 1000.0f / dtMs);

                                                // Remaining bytes across all depots
                                                float doneRatio = (static_cast<float>(depotsDone) + depotPct / 100.0f)
                                                                  / static_cast<float>(depotsTotal > 0 ? depotsTotal : 1);
                                                long long remainingBytes = static_cast<long long>(
                                                    totalBytes * (1.0f - doneRatio));
                                                etaSec = speedBps > 0
                                                    ? static_cast<int>(remainingBytes / speedBps)
                                                    : -1;
                                            }
                                        }
                                        lastPct  = depotPct;
                                        lastTime = now;

                                        // ── Overall percentage ───────────────────
                                        // Scale: 5%–95% for downloading phase
                                        int dt = depotsTotal > 0 ? depotsTotal : 1;
                                        int overall = 5 + static_cast<int>(
                                            90.0f * (static_cast<float>(depotsDone) + depotPct / 100.0f)
                                            / static_cast<float>(dt));
                                        overall = std::max(5, std::min(95, overall));
                                        if (overall != lastPercent)
                                        {
                                            writeProgressJson(progressPath, gameName,
                                                              depotsDone, depotsTotal, overall,
                                                              speedBps, etaSec);
                                            lastPercent = overall;
                                        }
                                    }
                                } catch (...) {}
                            }
                        }
                    }
                }
            }
            // Flush any partial line
            if (!lineBuf.empty())
                DD_LOG("DDM| %s\n", lineBuf.c_str());
        }

        close(pipefd[0]);

        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return -1;
    }

    // ── Legacy per-depot progress helper (used between depots) ───────────────

    static void writeDepotProgress(const std::string& progressPath,
                                   const std::string& gameName,
                                   int depotsDone,
                                   int depotsTotal)
    {
        if (progressPath.empty()) return;
        int dt = depotsTotal > 0 ? depotsTotal : 1;
        int percent = 5 + static_cast<int>(90.0 * depotsDone / dt);
        writeProgressJson(progressPath, gameName, depotsDone, depotsTotal, percent);
    }

    Result run(const LuaParser::ParseResult& lua,
               const std::string& destPath,
               const std::string& manifestDir,
               const std::string& progressPath,
               const std::vector<std::string>& requestedDepots)
    {
        Result result;

        if (!lua.valid || lua.appId.empty())
        {
            DD_LOG("invalid lua data\n");
            return result;
        }

        // ── Locate tools ──────────────────────────────────────────────────────
        const std::string dotnet = findDotnet();
        if (dotnet.empty())
        {
            DD_LOG("dotnet not found — install .NET 9 runtime\n");
            return result;
        }
        DD_LOG("dotnet -> %s\n", dotnet.c_str());

        const std::string ddmDll = findDepotDownloaderDll();
        if (ddmDll.empty())
        {
            DD_LOG("DepotDownloaderMod.dll not found\n");
            return result;
        }
        DD_LOG("DDM dll -> %s\n", ddmDll.c_str());

        std::string dotnetRoot;
        {
            fs::path p = fs::path(dotnet).parent_path();
            dotnetRoot = p.string();
        }

        // ── Download directory ────────────────────────────────────────────────
        std::string installFolder;
        if (const char* envDir = getenv("ACCELA_INSTALLDIR"); envDir && envDir[0] != '\0')
        {
            installFolder = envDir;
        }
        else
        {
            installFolder = sanitiseName(lua.gameName);
            if (installFolder.empty())
                installFolder = "App_" + lua.appId;
        }

        const std::string downloadDir =
            destPath + "/steamapps/common/" + installFolder;

        try { fs::create_directories(downloadDir); }
        catch (const std::exception& e)
        {
            DD_LOG("cannot create download dir %s: %s\n",
                     downloadDir.c_str(), e.what());
            return result;
        }
        result.downloadDir = downloadDir;
        DD_LOG("download dir -> %s\n", downloadDir.c_str());

        // ── Keep only the depots explicitly confirmed by the user ────────────
        std::vector<std::string> selectedDepots;
        for (const auto& id : requestedDepots)
        {
            const auto it = lua.depots.find(id);
            if (it == lua.depots.end()) continue;
            const auto& info = it->second;
            if (!info.key.empty() && !info.manifestGid.empty())
                selectedDepots.push_back(id);
            else
                DD_LOG("skipping depot %s (key=%s manifest=%s)\n",
                         id.c_str(),
                         info.key.empty()        ? "missing" : "ok",
                         info.manifestGid.empty() ? "missing" : "ok");
        }

        if (selectedDepots.empty())
        {
            DD_LOG("no user-selected downloadable depots for appid=%s\n",
                     lua.appId.c_str());
            return result;
        }

        result.depotsTotal = static_cast<int>(selectedDepots.size());

        for (const auto& id : selectedDepots)
        {
            const auto& info = lua.depots.at(id);
            if (!info.sizeBytes.empty())
            {
                try { result.totalBytes += std::stoull(info.sizeBytes); }
                catch (...) {}
            }
        }

        // ── Write keys VDF ────────────────────────────────────────────────────
        const std::string keysPath = writeKeysVdf(lua, selectedDepots);
        if (keysPath.empty()) return result;

        // Always use anonymous login — no Steam credentials required
        const char* accelaUsername = nullptr;

        const std::string manifestStagingDir =
            manifestDir.empty()
            ? (std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
               + "/mistwalker_manifests")
            : manifestDir;

        // Initial progress — preparing to download
        writeDepotProgress(progressPath, lua.gameName, 0, result.depotsTotal);

        // ── Per-depot download loop ───────────────────────────────────────────
        for (int i = 0; i < static_cast<int>(selectedDepots.size()); ++i)
        {
            const std::string& depotId = selectedDepots[i];
            const auto& info = lua.depots.at(depotId);

            DD_LOG("depot %s (%d/%d) manifest=%s\n",
                     depotId.c_str(),
                     i + 1, result.depotsTotal,
                     info.manifestGid.c_str());

            std::vector<std::string> cmd = {
                dotnet,
                ddmDll,
                "-app",          lua.appId,
                "-depot",        depotId,
                "-manifest",     info.manifestGid,
            };

            const std::string manifestFilePath =
                manifestStagingDir + "/" +
                depotId + "_" + info.manifestGid + ".manifest";

            if (fs::exists(manifestFilePath) &&
                fs::file_size(manifestFilePath) > 0)
            {
                cmd.push_back("-manifestfile");
                cmd.push_back(manifestFilePath);
            }

            cmd.insert(cmd.end(), {
                "-depotkeys",    keysPath,
                "-max-downloads","255",
                "-dir",          downloadDir,
                "-validate",
            });

            if (accelaUsername && *accelaUsername)
            {
                cmd.push_back("-username");
                cmd.push_back(accelaUsername);
            }
            else
            {
                cmd.push_back("-anonymous");
            }

            {
                std::ostringstream dbg;
                for (const auto& a : cmd) dbg << a << ' ';
                DD_LOG("cmd: %s\n", dbg.str().c_str());
            }

            const int rc = runAndStream(cmd, dotnetRoot,
                                       progressPath, lua.gameName,
                                       result.depotsDone, result.depotsTotal,
                                       result.totalBytes);

            if (rc == 0)
            {
                ++result.depotsDone;
                DD_LOG("depot %s finished OK\n", depotId.c_str());
            }
            else
            {
                DD_LOG("depot %s exited with code %d\n",
                         depotId.c_str(), rc);
            }

            // Update progress after each depot
            writeDepotProgress(progressPath, lua.gameName,
                               result.depotsDone, result.depotsTotal);
        }

        // ── Cleanup ───────────────────────────────────────────────────────────
        std::remove(keysPath.c_str());
        DD_LOG("removed keys VDF\n");

        result.ok = (result.depotsDone > 0);

        DD_LOG("finished %d/%d depots for appid=%s\n",
                 result.depotsDone, result.depotsTotal, lua.appId.c_str());

        return result;
    }

} // namespace CppAccela::DepotDownloader
