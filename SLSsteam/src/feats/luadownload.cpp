#include "luadownload.hpp"
#include "cdpinject.hpp"
#include "../config.hpp"
#include "../log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <unistd.h>

namespace LuaDownload
{
    // ── API provider list ──────────────────────────────────────────────

    struct ApiProvider
    {
        const char* name;
        const char* urlTemplate;  // <appid> is replaced with the actual app ID
        int successCode;
        int unavailableCode;
    };

    // Free APIs — same list as the LuaTools side project (api.json)
    static const ApiProvider g_apis[] = {
        {
            "Morrenus",
            "https://hubcapmanifest.com/api/v1/manifest/<appid>",
            200, 404
        },
        {
            "Ryuu (API Key)",
            "https://generator.ryuu.lol/resellerlua?appid=<appid>&source=SLSsteam&auth_code=",
            200, 404
        },
        {
            "Ryuu",
            "http://167.235.229.108/<appid>",
            200, 404
        },
        {
            "Sushi",
            "https://raw.githubusercontent.com/sushi-dev55-alt/sushitools-games-repo-alt/refs/heads/main/<appid>.zip",
            200, 404
        },
        {
            "Spinoza",
            "https://github.com/SPIN0ZAi/SB_manifest_DB/archive/refs/heads/<appid>.zip",
            200, 404
        },
        {
            "TwentyTwo Cloud",
            "https://twentytwocloud.com/secure_download?auth=1771526723_73652ce834428eea993e88dd1ccbe5be_442b8efb5c05f1bf8ea5ca46&appid=<appid>",
            200, 404
        },
    };

    // ── CDP status feedback ─────────────────────────────────────────────

    /**
     * Push a status update to the Steam store page button via CDP.
     * Finds the page matching /app/{appId} or /sub/{appId} and injects
     * JS to update the .sls-lua-btn button text and style.
     */
    static void pushStatus(const std::string& appId, const std::string& text,
                           const std::string& color = "")
    {
        auto pages = CDPInject::fetchPages();
        std::string matchApp = "/app/" + appId;
        std::string matchSub = "/sub/" + appId;

        for (auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty()) continue;
            if (page.url.find(matchApp) == std::string::npos &&
                page.url.find(matchSub) == std::string::npos)
                continue;

            // Build JS to update button
            // Escape single quotes in text
            std::string safeText = text;
            for (size_t i = 0; i < safeText.size(); ++i)
            {
                if (safeText[i] == '\'' || safeText[i] == '\\')
                {
                    safeText.insert(i, "\\");
                    ++i;
                }
            }

            std::string js = "(function(){";
            js += "var btns=document.querySelectorAll('.sls-lua-btn');";
            js += "btns.forEach(function(btn){";
            js += "var s=btn.querySelector('a span');";
            js += "if(s) s.innerText='" + safeText + "';";
            if (!color.empty())
            {
                js += "var a=btn.querySelector('a');";
                js += "if(a) a.style.filter='" + color + "';";
            }
            js += "});";
            if (text == "\u2705 Installed!")
            {
                js += "window.location.href='steam://install/" + appId + "';";
            }
            js += "})();";

            CDPInject::injectJS(page.webSocketDebuggerUrl, js);
            break; // Only need the first matching page
        }
    }

    // ── Steam path detection ───────────────────────────────────────────

    static std::string findSteamRoot()
    {
        const char* home = getenv("HOME");
        if (!home) return {};

        std::filesystem::path candidates[] = {
            std::filesystem::path(home) / ".local" / "share" / "Steam",
            std::filesystem::path(home) / ".steam" / "steam",
            std::filesystem::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
        };

        for (auto& path : candidates)
        {
            if (std::filesystem::exists(path / "steamui"))
                return path.string();
        }
        return {};
    }

    // ── libcurl helpers ────────────────────────────────────────────────

    static size_t curlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* file = static_cast<FILE*>(userdata);
        return fwrite(ptr, size, nmemb, file);
    }

    /**
     * Download a URL to a file using libcurl.
     * Returns the HTTP status code, or -1 on connection/curl error.
     */
    static int downloadToFile(const std::string& url, const std::string& destPath, const std::string& apiName, const std::string& authHeader = "")
    {
        CURL* curl = curl_easy_init();
        if (!curl) return -1;

        FILE* fp = fopen(destPath.c_str(), "wb");
        if (!fp)
        {
            curl_easy_cleanup(curl);
            return -1;
        }

        struct curl_slist* headers = NULL;
        if (!authHeader.empty()) {
            headers = curl_slist_append(headers, authHeader.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SLSsteam/1.0");
        // Disable signal-based timeout handling (safe for threads)
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (res != CURLE_OK)
        {
            g_pLog->debug("LuaDownload: curl error: %s\n", curl_easy_strerror(res));
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            std::remove(destPath.c_str());
            return -1;
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return static_cast<int>(httpCode);
    }

    // ── ZIP validation ─────────────────────────────────────────────────

    static bool isValidZip(const std::string& path)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;

        unsigned char magic[4] = {};
        size_t read = fread(magic, 1, 4, fp);
        fclose(fp);

        if (read < 4) return false;

        // PK\x03\x04 (normal), PK\x05\x06 (empty), PK\x07\x08 (spanned)
        return (magic[0] == 'P' && magic[1] == 'K' &&
                (magic[2] == 0x03 || magic[2] == 0x05 || magic[2] == 0x07));
    }

    // ── ZIP extraction ─────────────────────────────────────────────────

    /**
     * Extract a zip file to a directory using the unzip CLI.
     * Returns true on success.
     */
    static bool extractZip(const std::string& zipPath, const std::string& destDir)
    {
        std::filesystem::create_directories(destDir);

        std::string cmd = "unzip -o -q ";
        cmd += "\"" + zipPath + "\"";
        cmd += " -d \"" + destDir + "\"";
        cmd += " > /dev/null 2>&1";

        int ret = system(cmd.c_str());
        return (ret == 0);
    }

    // ── File discovery in extracted zip ────────────────────────────────

    struct ExtractedFiles
    {
        std::string luaFile;                    // Path to {appid}.lua
        std::vector<std::string> manifestFiles; // Paths to *.manifest
    };

    /**
     * Recursively search the extract directory for the .lua and .manifest files.
     */
    static ExtractedFiles findExtractedFiles(const std::string& extractDir, const std::string& appId)
    {
        ExtractedFiles result;
        std::string expectedLua = appId + ".lua";

        for (auto& entry : std::filesystem::recursive_directory_iterator(extractDir))
        {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();

            if (filename == expectedLua)
            {
                result.luaFile = entry.path().string();
            }
            else if (filename.size() > 9 && filename.substr(filename.size() - 9) == ".manifest")
            {
                result.manifestFiles.push_back(entry.path().string());
            }
        }

        return result;
    }

    // ── Main download & install ────────────────────────────────────────

    bool downloadAndInstall(const std::string& appId)
    {
        g_pLog->info("LuaDownload: Starting download for appid=%s\n", appId.c_str());
        pushStatus(appId, "\u23F3 Checking APIs...");

        // Find Steam root
        std::string steamRoot = findSteamRoot();
        if (steamRoot.empty())
        {
            g_pLog->info("LuaDownload: Could not find Steam installation\n");
            pushStatus(appId, "\u274C Steam not found", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }
        g_pLog->debug("LuaDownload: Steam root: %s\n", steamRoot.c_str());

        // Prepare paths
        std::string stplugDir = steamRoot + "/config/stplug-in";
        std::string depotcacheDir = steamRoot + "/config/depotcache";
        std::filesystem::create_directories(stplugDir);
        std::filesystem::create_directories(depotcacheDir);

        // Check if already installed
        std::string existingLua = stplugDir + "/" + appId + ".lua";
        if (std::filesystem::exists(existingLua))
        {
            g_pLog->info("LuaDownload: Lua script already exists for appid=%s, skipping\n", appId.c_str());
            pushStatus(appId, "\u2705 Already installed", "hue-rotate(110deg) brightness(1.2)");
            return true;
        }

        // Temp directory for this download
        const char* home = getenv("HOME");
        std::string tempDir = std::string(home ? home : "/tmp") + "/.cache/SLSsteam/downloads";
        std::filesystem::create_directories(tempDir);
        std::string zipPath = tempDir + "/" + appId + ".zip";
        std::string extractDir = tempDir + "/" + appId + "_extracted";

        // Try each API provider
        bool downloaded = false;
        std::string successApi;

        for (const auto& api : g_apis)
        {
            // Build URL from template
            std::string url = api.urlTemplate;
            size_t pos = url.find("<appid>");
            if (pos != std::string::npos)
            {
                url.replace(pos, 7, appId);
            }

            std::string authHeader;

            if (std::string(api.name) == "Morrenus")
            {
                authHeader = "Authorization: Bearer " + g_config.morrenusKey.get();
            }
            else if (std::string(api.name) == "Ryuu (API Key)")
            {
                url += g_config.ryuuKey.get(); // Still stored in ryuuKey config variable
            }

            g_pLog->info("LuaDownload: Trying API '%s' -> %s\n", api.name, url.c_str());
            pushStatus(appId, std::string("\u2B07 Trying ") + api.name + "...");

            // Download
            int httpCode = downloadToFile(url, zipPath, api.name, authHeader);
            g_pLog->debug("LuaDownload: API '%s' returned HTTP %d\n", api.name, httpCode);

            if (httpCode == api.unavailableCode)
            {
                g_pLog->debug("LuaDownload: API '%s' - not available (HTTP %d)\n", api.name, httpCode);
                std::remove(zipPath.c_str());
                continue;
            }

            if (httpCode != api.successCode)
            {
                g_pLog->debug("LuaDownload: API '%s' - unexpected status %d\n", api.name, httpCode);
                std::remove(zipPath.c_str());
                continue;
            }

            // Validate zip
            if (!isValidZip(zipPath))
            {
                g_pLog->info("LuaDownload: API '%s' returned non-zip file\n", api.name);
                std::remove(zipPath.c_str());
                continue;
            }

            downloaded = true;
            successApi = api.name;
            g_pLog->info("LuaDownload: Downloaded zip from '%s' for appid=%s\n", api.name, appId.c_str());
            break;
        }

        if (!downloaded)
        {
            g_pLog->info("LuaDownload: No API had the game for appid=%s\n", appId.c_str());
            pushStatus(appId, "\u274C Not available", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }

        // Extract zip
        // Clean up any previous extraction
        if (std::filesystem::exists(extractDir))
        {
            std::filesystem::remove_all(extractDir);
        }

        pushStatus(appId, "\U0001F4E6 Extracting...");
        if (!extractZip(zipPath, extractDir))
        {
            g_pLog->info("LuaDownload: Failed to extract zip for appid=%s\n", appId.c_str());
            pushStatus(appId, "\u274C Extract failed", "hue-rotate(0deg) brightness(1.0)");
            std::remove(zipPath.c_str());
            return false;
        }

        // Find the relevant files
        auto files = findExtractedFiles(extractDir, appId);

        if (files.luaFile.empty())
        {
            g_pLog->info("LuaDownload: No %s.lua found in the zip\n", appId.c_str());
            std::filesystem::remove_all(extractDir);
            std::remove(zipPath.c_str());
            return false;
        }

        g_pLog->info("LuaDownload: Found lua: %s, manifests: %zu\n",
                      files.luaFile.c_str(), files.manifestFiles.size());

        pushStatus(appId, "\U0001F527 Installing...");

        // Install manifest files to depotcache
        for (const auto& manifestPath : files.manifestFiles)
        {
            std::string filename = std::filesystem::path(manifestPath).filename().string();
            std::string destPath = depotcacheDir + "/" + filename;

            try
            {
                std::filesystem::copy_file(manifestPath, destPath,
                    std::filesystem::copy_options::overwrite_existing);
                g_pLog->info("LuaDownload: Installed manifest -> %s\n", destPath.c_str());
            }
            catch (const std::exception& e)
            {
                g_pLog->info("LuaDownload: Failed to copy manifest %s: %s\n",
                             filename.c_str(), e.what());
            }
        }

        // Install the .lua file
        try
        {
            std::filesystem::copy_file(files.luaFile, existingLua, std::filesystem::copy_options::overwrite_existing);
        }
        catch (const std::exception& e)
        {
            g_pLog->info("LuaDownload: Failed to copy lua file %s: %s\n", files.luaFile.c_str(), e.what());
            std::filesystem::remove_all(extractDir);
            std::remove(zipPath.c_str());
            return false;
        }

        g_pLog->info("LuaDownload: Installed lua -> %s (via %s)\n",
                      existingLua.c_str(), successApi.c_str());

        // Clean up temp files
        std::filesystem::remove_all(extractDir);
        std::remove(zipPath.c_str());

        g_pLog->info("LuaDownload: Successfully installed appid=%s\n", appId.c_str());
        pushStatus(appId, "\u2705 Installed!", "hue-rotate(110deg) brightness(1.2)");
        return true;
    }
}
