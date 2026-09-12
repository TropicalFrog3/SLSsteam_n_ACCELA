#include "luadownload.hpp"
#include "cdpinject.hpp"
#include "../config.hpp"
#include "../log.hpp"
#include "../globals.hpp"
#include "../CppAccela/accelapath.hpp"
#include "../CppAccela/accelazip.hpp"
#include "../CppAccela/accelaluaparser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>

namespace LuaDownload
{
    // ── API provider list ──────────────────────────────────────────────

    struct ApiProvider
    {
        const char* name;
        const char* urlTemplate;  // <appid> is replaced with the actual app ID
        int successCode;
        int unavailableCode;
        int timeoutSec;           // CURLOPT_TIMEOUT — 0 means use default (60)
    };

    static const ApiProvider g_apis[] = {
        {
            "Ryuu (API Key)",
            "https://generator.ryuu.lol/secure_download?appid=<appid>&source=SLSsteam-<steamid>&auth_code=",
            200, 404, 60
        },
        {
            "DepotBox",
            "https://depotbox.org/api/direct-download?appid=<appid>",
            200, 404, 900
        },
        {
            "Morrenus",
            "https://hubcapmanifest.com/api/v1/manifest/<appid>",
            200, 404, 60
        },
        {
            "Sushi",
            "https://raw.githubusercontent.com/sushi-dev55-alt/sushitools-games-repo-alt/refs/heads/main/<appid>.zip",
            200, 404, 60
        },
        {
            "Spinoza",
            "https://github.com/SPIN0ZAi/SB_manifest_DB/archive/refs/heads/<appid>.zip",
            200, 404, 60
        },
        {
            "TwentyTwo Cloud",
            "https://twentytwocloud.com/secure_download?auth=1771526723_73652ce834428eea993e88dd1ccbe5be_442b8efb5c05f1bf8ea5ca46&appid=<appid>",
            200, 404, 60
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
            if (text == "Installed!")
            {
                js += "window.location.href='steam://install/" + appId + "';";
            }
            js += "})();";

            CDPInject::injectJS(page.webSocketDebuggerUrl, js);
            break; // Only need the first matching page
        }
    }

    // ── Steam path detection ───────────────────────────────────────────

    // Delegates to CppAccela::Path — single source of truth for Steam root
    // detection shared across the whole CppAccela module.
    std::string findSteamRoot()
    {
        return CppAccela::Path::findSteamRoot();
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
    static int downloadToFile(const std::string& url, const std::string& destPath, const std::string& apiName, const std::string& authHeader = "", int timeoutSec = 60)
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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec > 0 ? timeoutSec : 60));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        
        // Use a more browser-like User-Agent to avoid some basic blocks
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        
        // Robust SSL settings
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Enable all supported encodings (gzip, deflate, etc.)
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        
        // Handle hanging connections
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

        // Disable signal-based timeout handling (safe for threads)
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (res != CURLE_OK)
        {
            LOG_DEBUG("LuaDownload: API '%s' curl error: %s (code %d)\n", apiName.c_str(), curl_easy_strerror(res), res);
            
            // If it's an SSL error on a known problematic host, suggest a reason
            if (res == CURLE_SSL_CONNECT_ERROR || res == CURLE_PEER_FAILED_VERIFICATION) {
                LOG_DEBUG("LuaDownload: TLS handshake failed — check certificates or DNS settings.\n");
            }

            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            std::filesystem::remove(destPath);
            return -1;
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return static_cast<int>(httpCode);
    }

    // ── ZIP validation ─────────────────────────────────────────────────

    // Delegates to CppAccela::Zip — single implementation, no duplication.
    static bool isValidZip(const std::string& path)
    {
        return CppAccela::Zip::isValid(path);
    }

    // ── ZIP extraction ─────────────────────────────────────────────────

    // Delegates to CppAccela::Zip::extract.
    static bool extractZip(const std::string& zipPath, const std::string& destDir)
    {
        return CppAccela::Zip::extract(zipPath, destDir);
    }

    // ── File discovery in extracted zip ────────────────────────────────

    // Re-use CppAccela::Zip::ExtractedFiles via a local alias so the rest
    // of this file can keep using the old struct name unchanged.
    using ExtractedFiles = CppAccela::Zip::ExtractedFiles;

    static ExtractedFiles findExtractedFiles(const std::string& extractDir,
                                             const std::string& appId)
    {
        return CppAccela::Zip::findFiles(extractDir, appId);
    }

    // ── Main download & install ────────────────────────────────────────

    bool downloadAndInstall(const std::string& appId, int providerIndex,
                            const std::vector<int>& providerOrder)
    {
        LOG_INFO("LuaDownload: Starting download for appid=%s\n", appId.c_str());
        pushStatus(appId, "Checking APIs...");

        // Find Steam root
        std::string steamRoot = findSteamRoot();
        if (steamRoot.empty())
        {
            LOG_INFO("LuaDownload: Could not find Steam installation\n");
            pushStatus(appId, "Steam not found", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }
        LOG_DEBUG("LuaDownload: Steam root: %s\n", steamRoot.c_str());

        // Prepare paths — delegated to CppAccela::Path (creates dirs if absent)
        std::string stplugDir    = CppAccela::Path::stplugDir();
        std::string depotcacheDir = CppAccela::Path::depotcacheDir();

        if (stplugDir.empty() || depotcacheDir.empty())
        {
            LOG_INFO("LuaDownload: failed to resolve Steam config directories\n");
            pushStatus(appId, "Steam not found", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }

        // Check if already installed
        std::string existingLua = stplugDir + "/" + appId + ".lua";
        if (std::filesystem::exists(existingLua))
        {
            // Verify the lua actually contains addappid(<appId>) — a lua from a
            // previous partial/mismatched install won't, and the config scanner
            // will mark it stale.  If it's bad, delete it and re-download.
            bool luaValid = false;
            {
                std::ifstream luaFile(existingLua);
                if (luaFile.is_open())
                {
                    std::string content((std::istreambuf_iterator<char>(luaFile)),
                                         std::istreambuf_iterator<char>());
                    luaValid = content.find("addappid(" + appId + ")") != std::string::npos;
                }
            }

            if (luaValid)
            {
                LOG_INFO("LuaDownload: Lua script already exists for appid=%s, skipping\n", appId.c_str());
                pushStatus(appId, "Already installed", "hue-rotate(110deg) brightness(1.2)");
                return true;
            }

            LOG_INFO("LuaDownload: Existing lua for appid=%s is invalid/mismatched, re-downloading\n", appId.c_str());
            std::filesystem::remove(existingLua);
        }

        // Temp directory for this download
        const char* home = getenv("HOME");
        std::string tempDir = std::string(home ? home : "/tmp") + "/.cache/SLSsteam/downloads";
        std::filesystem::create_directories(tempDir);
        std::string zipPath = tempDir + "/" + appId + ".zip";
        std::string extractDir = tempDir + "/" + appId + "_extracted";

        bool downloaded = false;
        std::string successApi;

        // Check if zip is already cached
        if (std::filesystem::exists(zipPath) && isValidZip(zipPath))
        {
            LOG_INFO("LuaDownload: Found cached zip for appid=%s\n", appId.c_str());
            downloaded = true;
            successApi = "Cache";
        }
        else
        {
            std::vector<int> apiOrder;
            if (providerIndex >= 0)
            {
                apiOrder.push_back(providerIndex);
            }
            else if (!providerOrder.empty())
            {
                apiOrder = providerOrder;
            }
            else
            {
                for (size_t i = 0; i < std::size(g_apis); ++i)
                    apiOrder.push_back(static_cast<int>(i));
            }

            // Try each selected API provider in the requested order.
            for (const int apiIndex : apiOrder)
            {
                if (apiIndex < 0 || apiIndex >= static_cast<int>(std::size(g_apis)))
                    continue;
                const auto& api = g_apis[apiIndex];
                // Build URL from template
                std::string url = api.urlTemplate;
                size_t pos = url.find("<appid>");
                if (pos != std::string::npos)
                {
                    url.replace(pos, 7, appId);
                }

                size_t steamidPos = url.find("<steamid>");
                if (steamidPos != std::string::npos)
                {
                    url.replace(steamidPos, 9, std::to_string(g_currentSteamId.steamId64));
                }

                std::string authHeader;

                if (std::string(api.name) == "Morrenus")
                {
                    authHeader = "Authorization: Bearer " + g_config.morrenusKey.get();
                }
                else if (std::string(api.name) == "Ryuu (API Key)")
                {
                    url += g_config.ryuuKey.get();
                }
                else if (std::string(api.name) == "DepotBox")
                {
                    if (g_config.depotBoxKey.get().empty())
                    {
                        LOG_DEBUG("LuaDownload: Skipping DepotBox (no API key configured)\n");
                        continue;
                    }
                    authHeader = "X-API-Key: " + g_config.depotBoxKey.get();
                }

                LOG_INFO("LuaDownload: Trying API '%s' -> %s\n", api.name, url.c_str());
                pushStatus(appId, std::string("Trying ") + api.name + "...");

                // Download
                int httpCode = downloadToFile(url, zipPath, api.name, authHeader, api.timeoutSec);
                LOG_DEBUG("LuaDownload: API '%s' returned HTTP %d\n", api.name, httpCode);

                // Steam browser fallback for Ryuu when curl fails (e.g. Cloudflare blocks direct requests)
                if (std::string(api.name) == "Ryuu (API Key)" && httpCode != api.successCode)
                {
                    LOG_INFO("LuaDownload: Ryuu curl failed (HTTP %d), trying Steam browser fallback...\n", httpCode);
                    pushStatus(appId, std::string("Trying ") + api.name + " (browser)...");
                    std::filesystem::remove(zipPath); // Remove any partial/invalid file from curl
                    httpCode = CDPInject::downloadViaPage(url, zipPath);
                    LOG_DEBUG("LuaDownload: Ryuu browser fallback returned HTTP %d\n", httpCode);
                }

                if (httpCode == api.unavailableCode)
                {
                    LOG_DEBUG("LuaDownload: API '%s' - not available (HTTP %d)\n", api.name, httpCode);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                if (httpCode != api.successCode)
                {
                    LOG_DEBUG("LuaDownload: API '%s' - unexpected status %d\n", api.name, httpCode);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                // Validate zip
                if (!isValidZip(zipPath))
                {
                    LOG_INFO("LuaDownload: API '%s' returned non-zip file\n", api.name);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                downloaded = true;
                successApi = api.name;
                LOG_INFO("LuaDownload: Downloaded zip from '%s' for appid=%s\n", api.name, appId.c_str());
                break;
            }
        }

        if (!downloaded)
        {
            LOG_INFO("LuaDownload: No API had the game for appid=%s\n", appId.c_str());
            pushStatus(appId, "Not available", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }

        // Extract zip
        // Clean up any previous extraction
        if (std::filesystem::exists(extractDir))
        {
            std::filesystem::remove_all(extractDir);
        }

        pushStatus(appId, "Extracting...");
        if (!extractZip(zipPath, extractDir))
        {
            LOG_INFO("LuaDownload: Failed to extract zip for appid=%s\n", appId.c_str());
            pushStatus(appId, "Extract failed", "hue-rotate(0deg) brightness(1.0)");
            std::filesystem::remove(zipPath);
            return false;
        }

        // Find the relevant files
        auto files = findExtractedFiles(extractDir, appId);

        if (files.luaFile.empty())
        {
            LOG_INFO("LuaDownload: No %s.lua found in the zip\n", appId.c_str());
            std::filesystem::remove_all(extractDir);
            std::filesystem::remove(zipPath);
            return false;
        }

        LOG_INFO("LuaDownload: Found lua: %s, manifests: %zu\n",
                      files.luaFile.c_str(), files.manifestFiles.size());

        // Validate the Lua file — must have at least one manifest
        {
            auto luaData = CppAccela::LuaParser::parseFile(files.luaFile);
            if (!luaData.valid || luaData.manifests.empty())
            {
                LOG_INFO("LuaDownload: Lua file has no valid manifests (via %s) — unusable\n",
                         successApi.c_str());
                std::filesystem::remove_all(extractDir);
                std::filesystem::remove(zipPath);
                pushStatus(appId, "Invalid Lua (no manifests)", "hue-rotate(0deg) brightness(1.0)");
                return false;
            }
        }

        pushStatus(appId, "Installing...");

        // Install manifest files to depotcache
        for (const auto& manifestPath : files.manifestFiles)
        {
            std::string filename = std::filesystem::path(manifestPath).filename().string();
            std::string destPath = depotcacheDir + "/" + filename;

            try
            {
                std::filesystem::copy_file(manifestPath, destPath,
                    std::filesystem::copy_options::overwrite_existing);
                LOG_INFO("LuaDownload: Installed manifest -> %s\n", destPath.c_str());
            }
            catch (const std::exception& e)
            {
                LOG_INFO("LuaDownload: Failed to copy manifest %s: %s\n",
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
            LOG_INFO("LuaDownload: Failed to copy lua file %s: %s\n", files.luaFile.c_str(), e.what());
            std::filesystem::remove_all(extractDir);
            std::filesystem::remove(zipPath);
            return false;
        }

        LOG_INFO("LuaDownload: Installed lua -> %s (via %s)\n",
                      existingLua.c_str(), successApi.c_str());

        // Clean up extracted files, but keep the zip cached
        std::filesystem::remove_all(extractDir);

        LOG_INFO("LuaDownload: Successfully installed appid=%s\n", appId.c_str());
        pushStatus(appId, "Installed!", "hue-rotate(110deg) brightness(1.2)");
        return true;
    }
}
