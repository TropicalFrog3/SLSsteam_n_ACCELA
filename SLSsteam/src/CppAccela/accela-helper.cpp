// accela-helper.cpp — standalone binary for ACCELA download pipeline
//
// This binary is exec()'d from SLSsteam after fork(). Running in a fresh
// process image eliminates all multithreading deadlocks that occur when
// calling complex C++ code directly after fork() in a multithreaded parent.
//
// Usage: accela-helper <appid>
//
// Exit codes:
//   0  - success
//   1  - generic failure
//   2  - invalid arguments
//   3  - Steam paths not found
//   4  - Lua plugin invalid
//   5  - no manifests resolved
//   6  - download failed (no depots downloaded)

#include "accelapath.hpp"
#include "accelamanifest.hpp"
#include "accelaluaparser.hpp"
#include "acceladepotdownloader.hpp"
#include "accelapostprocess.hpp"
#include "../atomic_file.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <algorithm>
#include <array>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <vector>

// Curl is linked into accela-helper via the Makefile HELPER_LDFLAGS
#include "../curl.hpp"

namespace fs = std::filesystem;

static void logMsg(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[AccelaHelper] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// ── PID file & Signal handling ───────────────────────────────────────────────
static std::string g_pidFilePath;

static void helperSignalHandler(int sig)
{
    CppAccela::DepotDownloader::killCurrentChild();
    if (!g_pidFilePath.empty()) unlink(g_pidFilePath.c_str());
    _exit(128 + sig);
}

struct PidFileGuard
{
    std::string path;
    ~PidFileGuard()
    {
        if (!path.empty()) unlink(path.c_str());
    }
};

// ── Progress file helpers ────────────────────────────────────────────────────
// Written to /tmp/sls_dl_<appid>.json so the parent process can read it.
// JSON is written atomically via a .tmp rename.

static std::string g_progressPath;

static std::vector<std::string> selectDepots(const CppAccela::LuaParser::ParseResult& lua)
{
    std::vector<std::string> depotIds;
    const char* envDepots = getenv("ACCELA_SELECTED_DEPOTS");
    
    if (envDepots && envDepots[0] != '\0')
    {
        std::string s(envDepots);
        size_t pos = 0;
        while ((pos = s.find(',')) != std::string::npos)
        {
            std::string d = s.substr(0, pos);
            if (!d.empty()) depotIds.push_back(d);
            s.erase(0, pos + 1);
        }
        if (!s.empty()) depotIds.push_back(s);
        logMsg("user selected %zu depots via UI\n", depotIds.size());
    }
    else
    {
        for (const auto& [depotId, info] : lua.depots)
        {
            if (!info.key.empty() && !info.manifestGid.empty())
                depotIds.push_back(depotId);
        }
        logMsg("automatically selected %zu depots\n", depotIds.size());
    }
    
    std::sort(depotIds.begin(), depotIds.end());
    return depotIds;
}

static void writeProgress(const char* phase,
                          const char* gameName,
                          int         depotsDone,
                          int         depotsTotal,
                          int         percent)
{
    if (g_progressPath.empty()) return;

    char buf[1024];
    // Escape gameName for JSON (handle quotes/backslashes)
    std::string safe;
    safe.reserve(strlen(gameName) + 4);
    for (const char* p = gameName; *p; ++p) {
        if (*p == '"' || *p == '\\') safe += '\\';
        safe += *p;
    }

    snprintf(buf, sizeof(buf),
             "{\"phase\":\"%s\",\"gameName\":\"%s\","
             "\"depotsDone\":%d,\"depotsTotal\":%d,\"percent\":%d}\n",
             phase, safe.c_str(), depotsDone, depotsTotal, percent);

    const std::string tmp = g_progressPath + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;
    fputs(buf, f);
    fclose(f);
    rename(tmp.c_str(), g_progressPath.c_str());
}

// ── Steam store name lookup ──────────────────────────────────────────────────
// Called when the Lua plugin has no --Gamename comment and no inline game name.
// Uses the Steam store API (public, no auth required) with a short timeout.
// Falls back to "App_<appid>" if the request fails or returns no name.

static std::string fetchGameName(const std::string& appIdStr)
{
    const std::string url =
        "https://store.steampowered.com/api/appdetails?appids="
        + appIdStr + "&filters=basic";

    std::string json;
    if (Curl::getString(url.c_str(), json) != 0 || json.empty())
    {
        logMsg("fetchGameName: curl failed for appid=%s\n", appIdStr.c_str());
        return {};
    }

    // Parse "name":"<value>" — minimal JSON extraction without a library.
    // The response is: {"<appid>":{"success":true,"data":{"name":"Game Title",...}}}
    const std::string needle = "\"name\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    size_t end = pos;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end += 2;
        } else if (json[end] == '"') {
            break;
        } else {
            end++;
        }
    }
    if (end >= json.size()) return {};

    std::string name = json.substr(pos, end - pos);

    // Unescape basic JSON sequences (\", \\, \/, \n, \t, \uXXXX→stripped)
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == '\\' && i + 1 < name.size())
        {
            ++i;
            if      (name[i] == '"')  out += '"';
            else if (name[i] == '\\') out += '\\';
            else if (name[i] == '/')  out += '/';
            else if (name[i] == 'n')  out += '\n';
            else if (name[i] == 't')  out += '\t';
            else if (name[i] == 'u' && i + 4 < name.size())
            {
                // \uXXXX — skip the 4 hex digits; non-ASCII chars are stripped
                // by sanitiseName() anyway so this keeps the name readable.
                i += 4;
            }
            else                      out += name[i];
        }
        else
        {
            out += name[i];
        }
    }

    logMsg("fetchGameName: resolved '%s' for appid=%s\n",
           out.c_str(), appIdStr.c_str());
    return out;
}

// ── JSON string decode helper ───────────────────────────────────────────────
static std::string decodeJsonString(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            if      (s[i] == '"')  out += '"';
            else if (s[i] == '\\') out += '\\';
            else if (s[i] == '/')  out += '/';
            else if (s[i] == 'b')  out += '\b';
            else if (s[i] == 'f')  out += '\f';
            else if (s[i] == 'n')  out += '\n';
            else if (s[i] == 'r')  out += '\r';
            else if (s[i] == 't')  out += '\t';
            else if (s[i] == 'u' && i + 4 < s.size())
            {
                auto parseHex = [](const std::string& str, size_t pos) -> uint32_t {
                    uint32_t val = 0;
                    for (int j = 0; j < 4; ++j) {
                        char c = str[pos + j];
                        val <<= 4;
                        if (c >= '0' && c <= '9') val |= (c - '0');
                        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
                        else return 0xFFFFFFFF;
                    }
                    return val;
                };

                uint32_t cp = parseHex(s, i + 1);
                i += 4;

                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u')
                {
                    uint32_t low = parseHex(s, i + 3);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }

                if (cp != 0xFFFFFFFF)
                {
                    if (cp <= 0x7F) {
                        out += static_cast<char>(cp);
                    } else if (cp <= 0x7FF) {
                        out += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp <= 0xFFFF) {
                        out += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp <= 0x10FFFF) {
                        out += static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
                        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                }
            }
            else {
                out += s[i];
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

// ── Steam CMD info lookup ───────────────────────────────────────────────────
// Fetches the 'installdir' from api.steamcmd.net if we don't have it via env.
static std::string fetchInstallDir(const std::string& appIdStr)
{
    const std::string url = "https://api.steamcmd.net/v1/info/" + appIdStr;

    std::string json;
    if (Curl::getString(url.c_str(), json) != 0 || json.empty())
    {
        logMsg("fetchInstallDir: curl failed for appid=%s\n", appIdStr.c_str());
        return {};
    }

    const std::string needle = "\"installdir\": \"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    
    size_t end = pos;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end += 2;
        } else if (json[end] == '"') {
            break;
        } else {
            end++;
        }
    }
    if (end >= json.size()) return {};

    std::string rawDir = json.substr(pos, end - pos);
    std::string installdir = decodeJsonString(rawDir);
    
    logMsg("fetchInstallDir: resolved '%s' for appid=%s\n",
           installdir.c_str(), appIdStr.c_str());
    return installdir;
}

int main(int argc, char* argv[])
{
    // debug sleep for attaching the accela-helper with gdb or lldb
    // sleep(30);
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <appid>\n", argv[0]);
        return 2;
    }

    const uint32_t appId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    if (appId == 0)
    {
        fprintf(stderr, "Invalid appid: %s\n", argv[1]);
        return 2;
    }

    const std::string appIdStr = std::to_string(appId);
    logMsg("starting pipeline for appid=%u\n", appId);

    // Set up progress file path
    const char* tmpEnvEarly = getenv("TMPDIR");
    const std::string tmpBaseEarly(tmpEnvEarly ? tmpEnvEarly : "/tmp");
    g_progressPath = tmpBaseEarly + "/sls_dl_" + appIdStr + ".json";
    writeProgress("starting", "", 0, 0, 0);

    // Write PID file for orphan detection
    g_pidFilePath = tmpBaseEarly + "/sls_dl_" + appIdStr + ".pid";
    {
        FILE* pf = fopen(g_pidFilePath.c_str(), "w");
        if (pf)
        {
            fprintf(pf, "%d\n", getpid());
            fclose(pf);
        }
    }
    PidFileGuard pidGuard{g_pidFilePath};

    signal(SIGTERM, helperSignalHandler);
    signal(SIGINT,  helperSignalHandler);
    signal(SIGHUP,  helperSignalHandler);

    // ── 1. Steam paths ────────────────────────────────────────────────────
    const std::string steamRoot = CppAccela::Path::findSteamRoot();
    if (steamRoot.empty())
    {
        logMsg("Steam root not found\n");
        return 3;
    }

    const std::string stplugPath     = CppAccela::Path::stplugDir();
    const std::string depotcachePath = CppAccela::Path::depotcacheDir();

    if (stplugPath.empty() || depotcachePath.empty())
    {
        logMsg("cannot resolve Steam config dirs\n");
        return 3;
    }

    logMsg("Steam root: %s\n", steamRoot.c_str());

    // ── 2. Parse Lua plugin ───────────────────────────────────────────────
    const std::string luaPath = stplugPath + "/" + appIdStr + ".lua";
    const auto lua = CppAccela::LuaParser::parseFile(luaPath);

    if (!lua.valid)
    {
        logMsg("Lua plugin invalid or missing: %s\n", luaPath.c_str());
        return 4;
    }

    logMsg("parsed Lua — game='%s' depots=%zu manifests=%zu\n",
           lua.gameName.c_str(), lua.depots.size(), lua.manifests.size());

    // If the Lua plugin has no game name (no --Gamename header, no inline comment),
    // fetch it from the Steam store API so the directory and ACF have the real title.
    auto luaMutable = lua;
    if (luaMutable.gameName.empty())
    {
        logMsg("game name missing from Lua — fetching from Steam API\n");
        luaMutable.gameName = fetchGameName(appIdStr);
        if (luaMutable.gameName.empty())
            logMsg("Steam API name lookup failed — will use App_%s\n", appIdStr.c_str());
        else
            logMsg("game name resolved to '%s'\n", luaMutable.gameName.c_str());
    }

    writeProgress("preparing", luaMutable.gameName.c_str(), 0, 0, 3);

    // If ACCELA_INSTALLDIR is not set by the parent, try fetching it from steamcmd API
    if (const char* envDir = getenv("ACCELA_INSTALLDIR"); !envDir || envDir[0] == '\0')
    {
        logMsg("ACCELA_INSTALLDIR not set — fetching from Steam API\n");
        std::string fetchedInstallDir = fetchInstallDir(appIdStr);
        if (!fetchedInstallDir.empty())
        {
            setenv("ACCELA_INSTALLDIR", fetchedInstallDir.c_str(), 1);
            logMsg("set ACCELA_INSTALLDIR='%s'\n", fetchedInstallDir.c_str());
        }
    }

    const auto selectedDepots = selectDepots(luaMutable);
    if (selectedDepots.empty())
    {
        logMsg("no depots selected; aborting before download\n");
        return 0;
    }

    // ── 3. Collect manifest files ─────────────────────────────────────────
    std::vector<CppAccela::Manifest::DepotManifest> manifestPairs;
    manifestPairs.reserve(luaMutable.manifests.size());
    for (const auto& [depotId, manifestGid] : luaMutable.manifests)
        manifestPairs.push_back({ depotId, manifestGid });

    if (manifestPairs.empty())
    {
        logMsg("no setManifestid() entries for appid=%u\n", appId);
        return 5;
    }

    const char* tmpEnv = getenv("TMPDIR");
    const std::string tmpBase(tmpEnv ? tmpEnv : "/tmp");
    const std::string manifestStagingDir = tmpBase + "/mistwalker_manifests";
    const std::string placeholderDir     = tmpBase + "/accela_placeholders";

    fs::create_directories(manifestStagingDir);

    const auto collected = CppAccela::Manifest::collectManifestFiles(
        appIdStr, manifestPairs, depotcachePath, placeholderDir);

    if (!collected.ok)
    {
        logMsg("no manifests resolved for appid=%u — aborting\n", appId);
        return 5;
    }

    // Stage collected files under their canonical name
    for (const auto& srcPath : collected.manifestFiles)
    {
        const std::string fname = fs::path(srcPath).filename().string();
        const std::string dst   = manifestStagingDir + "/" + fname;
        if (srcPath != dst)
        {
            std::error_code ec;
            fs::copy_file(srcPath, dst,
                          fs::copy_options::overwrite_existing, ec);
            if (ec)
                logMsg("copy manifest %s: %s\n",
                          fname.c_str(), ec.message().c_str());
        }
    }

    logMsg("%zu manifest(s) staged in %s\n",
           collected.manifestFiles.size(), manifestStagingDir.c_str());

    // ── 4. Download depots ────────────────────────────────────────────────
    const CppAccela::DepotDownloader::Result dlResult = CppAccela::DepotDownloader::run(
        luaMutable, steamRoot, manifestStagingDir, g_progressPath, selectedDepots);

    logMsg("download finished — %d/%d depots, ~%llu bytes\n",
           dlResult.depotsDone, dlResult.depotsTotal,
           static_cast<unsigned long long>(dlResult.totalBytes));

    if (!dlResult.ok || dlResult.depotsDone == 0 || dlResult.depotsDone < dlResult.depotsTotal)
    {
        logMsg("ERROR: depot download failed or incomplete (%d/%d depots completed) — aborting without post-processing\n",
               dlResult.depotsDone, dlResult.depotsTotal);
        writeProgress("failed", luaMutable.gameName.c_str(), dlResult.depotsDone, dlResult.depotsTotal, 0);
        AtomicFile::removePath(steamRoot + "/steamapps/downloading/" + appIdStr);
        return 6;
    }

    writeProgress("postprocessing", luaMutable.gameName.c_str(),
                  dlResult.depotsDone, dlResult.depotsTotal, 96);

    // ── 5. Post-processing ────────────────────────────────────────────────
    std::string configPath;
    {
        const char* xdgCfg = getenv("XDG_CONFIG_HOME");
        const char* home   = getenv("HOME");
        if (xdgCfg && xdgCfg[0] == '/')
            configPath = std::string(xdgCfg) + "/SLSsteam/config.yaml";
        else if (home)
            configPath = std::string(home) + "/.config/SLSsteam/config.yaml";
    }

    const CppAccela::PostProcess::Context ppCtx {
        luaMutable,
        dlResult,
        steamRoot,
        manifestStagingDir,
        depotcachePath,
        configPath,
    };

    const bool ppOk = CppAccela::PostProcess::run(ppCtx);

    if (!ppOk)
        logMsg("WARNING: post-processing had errors for appid=%u\n", appId);

    logMsg("pipeline complete for appid=%u\n", appId);

    writeProgress("done", luaMutable.gameName.c_str(),
                  dlResult.depotsDone, dlResult.depotsTotal, 100);
    return 0;
}
