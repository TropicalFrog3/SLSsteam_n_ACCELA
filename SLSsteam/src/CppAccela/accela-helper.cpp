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

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return {};

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
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return {};

    std::string installdir = json.substr(pos, end - pos);
    
    logMsg("fetchInstallDir: resolved '%s' for appid=%s\n",
           installdir.c_str(), appIdStr.c_str());
    return installdir;
}

int main(int argc, char* argv[])
{
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

    if (!dlResult.ok)
        logMsg("WARNING: depot download incomplete — continuing to post-process\n");

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

    if (dlResult.depotsDone == 0)
    {
        writeProgress("failed", luaMutable.gameName.c_str(), 0, dlResult.depotsTotal, 0);
        return 6;
    }

    writeProgress("done", luaMutable.gameName.c_str(),
                  dlResult.depotsDone, dlResult.depotsTotal, 100);
    return 0;
}
