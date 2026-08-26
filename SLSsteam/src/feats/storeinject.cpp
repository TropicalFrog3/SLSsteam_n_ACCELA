#include "storeinject.hpp"
#include "cdpinject.hpp"
#include "luadownload.hpp"
#include "removelua.hpp"
#include "apps.hpp"
#include "../sdk/IClientAppManager.hpp"
#include "../sdk/IClientApps.hpp"
#include "../log.hpp"
#include "../config.hpp"
#include "../utils.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sys/wait.h>
#include <regex>
#include <fstream>
#include <cctype>
#include <base64/base64.hpp>


static std::string urlDecode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                int value;
                std::istringstream is(str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    ret += static_cast<char>(value);
                    i += 2;
                } else ret += '%';
            } else ret += '%';
        } else if (str[i] == '+') ret += ' ';
        else ret += str[i];
    }
    return ret;
}
#include <sstream>
#include <string>
#include <algorithm>
#include <set>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

namespace StoreInject
{
    static std::thread g_workerThread;
    static std::thread g_autoThread;
    static std::set<uint32_t> g_pendingRestartApps;
    static std::atomic<bool> g_shouldStop(false);

    struct ManualFile {
        std::string name;
        std::string base64Content;
    };

    static std::string readFullHttpRequest(int sock)
    {
        std::string request;
        char buffer[4096];
        ssize_t n;
        
        // Set a receive timeout on the socket
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        // Read headers
        size_t header_end = std::string::npos;
        while (header_end == std::string::npos)
        {
            n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0)
            {
                break;
            }
            buffer[n] = '\0';
            request.append(buffer, n);
            header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                header_end = request.find("\n\n");
            }
        }
        
        if (header_end == std::string::npos)
        {
            return request;
        }
        
        // Find Content-Length
        size_t content_length_pos = request.find("Content-Length:");
        if (content_length_pos == std::string::npos)
        {
            content_length_pos = request.find("content-length:");
        }
        
        size_t content_length = 0;
        if (content_length_pos != std::string::npos && content_length_pos < header_end)
        {
            size_t value_start = request.find_first_not_of(" \t", content_length_pos + 15);
            size_t value_end = request.find_first_of("\r\n", value_start);
            if (value_start != std::string::npos && value_end != std::string::npos)
            {
                try
                {
                    content_length = std::stoul(request.substr(value_start, value_end - value_start));
                }
                catch (...) {}
            }
        }
        
        // Read the remaining body
        size_t header_len = (request.find("\r\n\r\n") != std::string::npos) ? (header_end + 4) : (header_end + 2);
        size_t body_received = request.size() - header_len;
        
        while (body_received < content_length)
        {
            size_t to_read = std::min(sizeof(buffer) - 1, content_length - body_received);
            n = recv(sock, buffer, to_read, 0);
            if (n <= 0)
            {
                break;
            }
            buffer[n] = '\0';
            request.append(buffer, n);
            body_received += n;
        }
        
        return request;
    }

    static std::string handleManualInstall(const std::string& request)
    {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string body = (bodyPos == std::string::npos) ? "" : request.substr(bodyPos + 4);
        if (body.empty()) {
            return "{\"success\":false,\"message\":\"Empty request body.\"}";
        }

        // Parse appid using simple string search (avoid regex on huge body)
        std::string appId;
        size_t appidKeyPos = body.find("\"appid\"");
        if (appidKeyPos != std::string::npos) {
            size_t colonPos = body.find(':', appidKeyPos + 7);
            if (colonPos != std::string::npos) {
                // Skip whitespace after colon
                size_t valStart = colonPos + 1;
                while (valStart < body.size() && (body[valStart] == ' ' || body[valStart] == '\t')) valStart++;
                if (valStart < body.size()) {
                    if (body[valStart] == '"') {
                        // String value: "12345"
                        size_t valEnd = body.find('"', valStart + 1);
                        if (valEnd != std::string::npos)
                            appId = body.substr(valStart + 1, valEnd - valStart - 1);
                    } else if (std::isdigit(body[valStart])) {
                        // Numeric value: 12345
                        size_t valEnd = valStart;
                        while (valEnd < body.size() && std::isdigit(body[valEnd])) valEnd++;
                        appId = body.substr(valStart, valEnd - valStart);
                    }
                }
            }
        }
        if (appId.empty()) {
            return "{\"success\":false,\"message\":\"AppID not found in request.\"}";
        }

        // Parse files array using JSON-string-aware parsing
        // We need to find each {"name":"...","content":"..."} object
        // but base64 content can contain ], }, etc. so we must respect JSON string boundaries
        std::vector<ManualFile> files;
        size_t filesKeyPos = body.find("\"files\"");
        if (filesKeyPos != std::string::npos) {
            size_t startBracket = body.find("[", filesKeyPos);
            if (startBracket != std::string::npos) {
                size_t pos = startBracket + 1;
                while (pos < body.size()) {
                    // Skip whitespace and commas
                    while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',' || body[pos] == '\n' || body[pos] == '\r' || body[pos] == '\t'))
                        pos++;
                    if (pos >= body.size() || body[pos] == ']') break;
                    if (body[pos] != '{') break;

                    // Find the matching closing brace by tracking JSON string boundaries
                    size_t objStart = pos;
                    int braceDepth = 0;
                    bool inString = false;
                    bool escaped = false;
                    size_t objEnd = std::string::npos;

                    for (size_t i = pos; i < body.size(); i++) {
                        char c = body[i];
                        if (escaped) {
                            escaped = false;
                            continue;
                        }
                        if (c == '\\' && inString) {
                            escaped = true;
                            continue;
                        }
                        if (c == '"') {
                            inString = !inString;
                            continue;
                        }
                        if (inString) continue;
                        if (c == '{') braceDepth++;
                        else if (c == '}') {
                            braceDepth--;
                            if (braceDepth == 0) {
                                objEnd = i;
                                break;
                            }
                        }
                    }

                    if (objEnd == std::string::npos) break;

                    // Extract name and content from the object using simple string search
                    std::string fileObject = body.substr(objStart, objEnd - objStart + 1);
                    pos = objEnd + 1;

                    std::string name;
                    std::string content;

                    // Find "name":"value"
                    size_t nameKeyPos = fileObject.find("\"name\"");
                    if (nameKeyPos != std::string::npos) {
                        size_t nc = fileObject.find(':', nameKeyPos + 6);
                        if (nc != std::string::npos) {
                            size_t ns = fileObject.find('"', nc + 1);
                            if (ns != std::string::npos) {
                                size_t ne = fileObject.find('"', ns + 1);
                                if (ne != std::string::npos) {
                                    name = fileObject.substr(ns + 1, ne - ns - 1);
                                }
                            }
                        }
                    }

                    // Find "content":"value" - content is base64 so no escaped quotes inside
                    size_t contentKeyPos = fileObject.find("\"content\"");
                    if (contentKeyPos != std::string::npos) {
                        size_t cc = fileObject.find(':', contentKeyPos + 9);
                        if (cc != std::string::npos) {
                            size_t cs = fileObject.find('"', cc + 1);
                            if (cs != std::string::npos) {
                                // Find the closing quote - base64 won't contain unescaped quotes
                                size_t ce = fileObject.find('"', cs + 1);
                                if (ce != std::string::npos) {
                                    content = fileObject.substr(cs + 1, ce - cs - 1);
                                }
                            }
                        }
                    }

                    if (!name.empty() && !content.empty()) {
                        files.push_back({name, content});
                    }
                }
            }
        }

        if (files.empty()) {
            return "{\"success\":false,\"message\":\"No files to install.\"}";
        }

        std::string steamRoot = LuaDownload::findSteamRoot();
        if (steamRoot.empty()) {
            return "{\"success\":false,\"message\":\"Steam root directory not found.\"}";
        }

        std::filesystem::path depotcache = std::filesystem::path(steamRoot) / "config" / "depotcache";
        std::filesystem::path stplugin = std::filesystem::path(steamRoot) / "config" / "stplug-in";
        std::filesystem::create_directories(depotcache);
        std::filesystem::create_directories(stplugin);

        int luaCount = 0;
        int manifestCount = 0;

        // First, check if there is a single lua file or a lua file matching appid
        std::string mainLuaIndex = "";
        int totalLuaFiles = 0;
        for (const auto& file : files) {
            std::string ext = std::filesystem::path(file.name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".lua") {
                totalLuaFiles++;
                std::string fname = std::filesystem::path(file.name).filename().string();
                if (fname == appId + ".lua" || fname.find(appId) != std::string::npos) {
                    mainLuaIndex = file.name;
                }
            }
        }

        for (const auto& file : files) {
            std::string ext = std::filesystem::path(file.name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            std::string decodedData;
            try {
                decodedData = base64::from_base64(file.base64Content);
            } catch (...) {
                continue;
            }
            
            if (ext == ".zip") {
                std::string zipPath = "/tmp/sls_manual_" + appId + ".zip";
                std::ofstream zf(zipPath, std::ios::binary | std::ios::trunc);
                if (!zf) {
                    return "{\"success\":false,\"message\":\"Failed to create temporary zip file.\"}";
                }
                zf.write(decodedData.data(), decodedData.size());
                zf.close();

                bool validZip = false;
                if (decodedData.size() >= 4) {
                    validZip = (decodedData[0] == 'P' && decodedData[1] == 'K' &&
                               (decodedData[2] == 0x03 || decodedData[2] == 0x05 || decodedData[2] == 0x07));
                }
                if (!validZip) {
                    std::filesystem::remove(zipPath);
                    return "{\"success\":false,\"message\":\"Uploaded file " + file.name + " is not a valid zip archive.\"}";
                }

                std::string extractDir = "/tmp/sls_manual_dir_" + appId;
                std::filesystem::remove_all(extractDir);
                std::filesystem::create_directories(extractDir);

                pid_t pid = fork();
                if (pid == 0) {
                    execlp("unzip", "unzip", "-o", "-q", zipPath.c_str(), "-d", extractDir.c_str(), nullptr);
                    _exit(127);
                }

                int status = 0;
                waitpid(pid, &status, 0);
                std::filesystem::remove(zipPath);

                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    std::filesystem::remove_all(extractDir);
                    return "{\"success\":false,\"message\":\"Failed to extract zip archive using unzip command.\"}";
                }

                std::vector<std::string> zipLuaFiles;
                std::vector<std::string> zipManifestFiles;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(extractDir)) {
                    if (!entry.is_regular_file()) continue;
                    std::string zext = entry.path().extension().string();
                    std::transform(zext.begin(), zext.end(), zext.begin(), ::tolower);
                    if (zext == ".lua") {
                        zipLuaFiles.push_back(entry.path().string());
                    } else if (zext == ".manifest") {
                        zipManifestFiles.push_back(entry.path().string());
                    }
                }

                if (zipLuaFiles.empty() && zipManifestFiles.empty()) {
                    std::filesystem::remove_all(extractDir);
                    return "{\"success\":false,\"message\":\"No .lua or .manifest files found inside the zip archive.\"}";
                }

                for (const auto& mf : zipManifestFiles) {
                    std::string dest = (depotcache / std::filesystem::path(mf).filename()).string();
                    try {
                        std::filesystem::copy_file(mf, dest, std::filesystem::copy_options::overwrite_existing);
                        manifestCount++;
                    } catch (...) {}
                }

                if (!zipLuaFiles.empty()) {
                    std::string selectedLua;
                    for (const auto& lf : zipLuaFiles) {
                        std::string fname = std::filesystem::path(lf).filename().string();
                        if (fname == appId + ".lua" || fname.find(appId) != std::string::npos) {
                            selectedLua = lf;
                            break;
                        }
                    }
                    if (selectedLua.empty()) {
                        selectedLua = zipLuaFiles[0];
                    }
                    std::string dest = (stplugin / (appId + ".lua")).string();
                    try {
                        std::filesystem::copy_file(selectedLua, dest, std::filesystem::copy_options::overwrite_existing);
                        luaCount++;
                    } catch (...) {}
                }

                std::filesystem::remove_all(extractDir);
            }
            else if (ext == ".manifest") {
                std::string dest = (depotcache / std::filesystem::path(file.name).filename()).string();
                std::ofstream f(dest, std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write(decodedData.data(), decodedData.size());
                    f.close();
                    manifestCount++;
                }
            }
            else if (ext == ".lua") {
                std::string destFilename = std::filesystem::path(file.name).filename().string();
                std::filesystem::path destPath;
                if (destFilename == appId + ".lua" || file.name == mainLuaIndex || (totalLuaFiles == 1 && mainLuaIndex.empty())) {
                    destPath = stplugin / (appId + ".lua");
                } else {
                    destPath = stplugin / file.name;
                }
                try {
                    std::filesystem::create_directories(destPath.parent_path());
                    std::ofstream f(destPath, std::ios::binary | std::ios::trunc);
                    if (f) {
                        f.write(decodedData.data(), decodedData.size());
                        f.close();
                        luaCount++;
                    }
                } catch (...) {}
            }
        }

        uint32_t appIdVal = std::stoul(appId);

        std::string msg = "Successfully installed";
        if (luaCount > 0) {
            msg += " " + std::to_string(luaCount) + " Lua script" + (luaCount > 1 ? "s" : "");
        }
        if (manifestCount > 0) {
            if (luaCount > 0) msg += " and";
            msg += " " + std::to_string(manifestCount) + " manifest(s)";
        }
        msg += ".";
        return "{\"success\":true,\"message\":\"" + msg + "\"}";
    }

    // Marker comments used to identify injected content in index.html
    static const char* INJECT_MARKER_BEGIN = "<!-- SLS_STORE_INJECT_V7_BEGIN -->";
    static const char* INJECT_MARKER_END   = "<!-- SLS_STORE_INJECT_V7_END -->";

    /**
     * Locate Steam's steamui/index.html.
     */
    static std::filesystem::path findSteamUIIndexHtml()
    {
        const char* home = getenv("HOME");
        if (!home) return {};

        std::filesystem::path candidates[] = {
            std::filesystem::path(home) / ".local" / "share" / "Steam" / "steamui" / "index.html",
            std::filesystem::path(home) / ".steam" / "steam" / "steamui" / "index.html",
            std::filesystem::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam" / "steamui" / "index.html",
        };

        for (auto& path : candidates)
        {
            if (std::filesystem::exists(path)) return path;
        }
        return {};
    }

    // generateInjectionJS and patchIndexHtml removed.
    // Cross-context Javascript execution is no longer allowed in modern Steam UI.
    // We rely exclusively on inject_cef.py (CDP) and URL hash polling.

    static void automationWorker()
    {
        LOG_INFO("StoreInject: Automation worker started\n");
        
        std::string lastProcessedTimestamp = "";
        int checkCounter = 0;

        while (!g_shouldStop)
        {
            // Try to read CDP /json endpoint for page URLs
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0)
            {
                struct sockaddr_in serv_addr;
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(8080);
                inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

                struct timeval tv;
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0)
                {
                    // Send HTTP GET /json
                    const char* httpReq = "GET /json HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nConnection: close\r\n\r\n";
                    send(sock, httpReq, strlen(httpReq), 0);

                    std::string response;
                    char buf[4096];
                    int n;
                    while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
                        buf[n] = '\0';
                        response += buf;
                    }

                    // Check for #sls-click-removelua- in any URL
                    size_t removeSearchPos = 0;
                    while ((removeSearchPos = response.find("#sls-click-removelua-", removeSearchPos)) != std::string::npos)
                    {
                        size_t idStart = removeSearchPos + 21; // after "#sls-click-removelua-"
                        size_t idEnd = response.find_first_of("-\"", idStart);
                        if (idEnd != std::string::npos && idEnd > idStart && response[idEnd] == '-')
                        {
                            size_t tsEnd = response.find_first_of("\"", idEnd + 1);
                            if (tsEnd != std::string::npos)
                            {
                                std::string productId = response.substr(idStart, idEnd - idStart);
                                std::string timestamp = response.substr(idEnd + 1, tsEnd - (idEnd + 1));
                                
                                if (timestamp != lastProcessedTimestamp)
                                {
                                    lastProcessedTimestamp = timestamp;
                                    LOG_INFO("Remove Lua clicked for Product ID: %s\n", productId.c_str());

                                    // Trigger removal in background thread
                                    std::string pid = productId;
                                    std::thread([pid]() {
                                        try {
                                            uint32_t appId = std::stoul(pid);
                                            
                                            // 1. Delete game files
                                            Apps::deleteGameFiles(appId);
                                            
                                            // 2. Remove Lua and Manifest files
                                            std::string pluginDir = g_config.getPluginDir();
                                            if (!pluginDir.empty()) {
                                                // Remove lua
                                                auto luaPath = std::filesystem::path(pluginDir) / (pid + ".lua");
                                                if (std::filesystem::exists(luaPath)) {
                                                    std::filesystem::remove(luaPath);
                                                    LOG_INFO("RemoveLua: Deleted lua file %s\n", luaPath.c_str());
                                                }
                                                
                                                // Remove manifests from pluginDir
                                                for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                                    if (entry.is_regular_file()) {
                                                        auto path = entry.path();
                                                        if (path.extension() == ".manifest") {
                                                            std::string stem = path.stem().string();
                                                            size_t underscorePos = stem.find('_');
                                                            std::string appIdStr = (underscorePos != std::string::npos) ? stem.substr(0, underscorePos) : stem;
                                                            if (appIdStr == pid) {
                                                                std::filesystem::remove(path);
                                                                LOG_INFO("RemoveLua: Deleted manifest %s from pluginDir\n", path.c_str());
                                                            }
                                                        }
                                                    }
                                                }
                                                
                                                // Remove manifests from depotcache
                                                std::filesystem::path configPath = std::filesystem::path(pluginDir).parent_path();
                                                std::filesystem::path depotcachePath = configPath / "depotcache";
                                                if (std::filesystem::exists(depotcachePath)) {
                                                    for (const auto& entry : std::filesystem::directory_iterator(depotcachePath)) {
                                                        if (entry.is_regular_file()) {
                                                            auto path = entry.path();
                                                            if (path.extension() == ".manifest") {
                                                                std::string stem = path.stem().string();
                                                                size_t underscorePos = stem.find('_');
                                                                std::string appIdStr = (underscorePos != std::string::npos) ? stem.substr(0, underscorePos) : stem;
                                                                if (appIdStr == pid) {
                                                                    std::filesystem::remove(path);
                                                                    LOG_INFO("RemoveLua: Deleted manifest %s from depotcache\n", path.c_str());
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            
                                            // 3. Update Config
                                            g_config.removeAdditionalAppId(appId);
                                            
                                            LOG_INFO("RemoveLua: Completed for appid=%s\n", pid.c_str());
                                        } catch (const std::exception& e) {
                                            LOG_WARN("RemoveLua: Error processing appid %s: %s\n", pid.c_str(), e.what());
                                        }
                                    }).detach();
                                }
                            }
                        }
                        removeSearchPos = idStart;
                    }

                    // Check for #sls-click- in any URL
                    size_t searchPos = 0;
                    while ((searchPos = response.find("#sls-click-", searchPos)) != std::string::npos)
                    {
                        size_t idStart = searchPos + 11; // after "#sls-click-"
                        size_t idEnd = response.find_first_of("-\"", idStart);
                        if (idEnd != std::string::npos && idEnd > idStart && response[idEnd] == '-')
                        {
                            size_t tsEnd = response.find_first_of("\"", idEnd + 1);
                            if (tsEnd != std::string::npos)
                            {
                                std::string productId = response.substr(idStart, idEnd - idStart);
                                std::string timestamp = response.substr(idEnd + 1, tsEnd - (idEnd + 1));
                                
                                if (timestamp != lastProcessedTimestamp)
                                {
                                    lastProcessedTimestamp = timestamp;
                                    LOG_INFO("Download Lua clicked for Product ID: %s\n", productId.c_str());

                                    // Trigger download in background thread
                                    std::string pid = productId; // copy for lambda capture
                                    std::thread([pid]() {
                                        bool ok = LuaDownload::downloadAndInstall(pid);
                                        if (ok)
                                        {
                                            LOG_INFO("LuaDownload: Completed for appid=%s\n", pid.c_str());

                                            // Ensure the game shows in library even if the user
                                            // navigated away from the store page (which would
                                            // prevent the CDP steam://install/ from firing).
                                            try {
                                                uint32_t appId = std::stoul(pid);
                                                g_config.addAdditionalAppId(appId);
                                                // Apps::setInstalled(appId);
                                                // scanLuaPluginsAndUpdateConfig();
                                            } catch (...) {
                                                LOG_WARN("LuaDownload: Failed to register appid=%s in library\n", pid.c_str());
                                            }
                                        }
                                        else
                                            LOG_INFO("LuaDownload: Failed for appid=%s\n", pid.c_str());
                                    }).detach();
                                }
                            }
                        }
                        searchPos = idStart;
                    }

                    // Check for #sls-auth-MORR=
                    searchPos = 0;
                    while ((searchPos = response.find("#sls-auth-MORR=", searchPos)) != std::string::npos)
                    {
                        size_t mStart = searchPos + 15;
                        size_t mEnd = response.find("&RYUU=", mStart);
                        if (mEnd != std::string::npos)
                        {
                            size_t rStart = mEnd + 6;
                            size_t cEnd = response.find("-TS=", rStart);
                            if (cEnd != std::string::npos)
                            {
                                std::string morr = urlDecode(response.substr(mStart, mEnd - mStart));
                                std::string ryuu = urlDecode(response.substr(rStart, cEnd - rStart));
                                std::string timestamp = response.substr(cEnd + 4, response.find_first_of("\"", cEnd + 4) - (cEnd + 4));

                                if (timestamp != lastProcessedTimestamp)
                                {
                                    lastProcessedTimestamp = timestamp;
                                    LOG_INFO("API Settings received via CDP UI! Updating config...\n");
                                    if (!morr.empty()) g_config.morrenusKey = morr;
                                    if (!ryuu.empty()) g_config.ryuuKey = ryuu;
                                    g_config.updateApiAuth(g_config.morrenusKey.get(), g_config.ryuuKey.get());
                                }
                            }
                        }
                        searchPos = mStart;
                    }

                    // Trigger the C++ CDP injector every 5 seconds to inject new pages
                    if (checkCounter++ >= 1)
                    {
                        CDPInject::injectStorePages();
                        RemoveLua::injectRemoveLuaScript();
                        checkCounter = 0;
                    }
                }
                close(sock);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    static void callbackServer()
    {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9001);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            close(server_fd);
            return;
        }
        if (listen(server_fd, 5) < 0)
        {
            close(server_fd);
            return;
        }

        LOG_INFO("StoreInject: Callback server listening on port 9001\n");

        while (!g_shouldStop)
        {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);

            if (select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0)
            {
                if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0)
                {
                    std::string request = readFullHttpRequest(new_socket);
                    bool handled = false;
                    if (!request.empty())
                    {
                        // Handle CORS preflight (OPTIONS) requests for all endpoints
                        if (request.rfind("OPTIONS ", 0) == 0)
                        {
                            const char* preflightResponse =
                                "HTTP/1.1 204 No Content\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                                "Access-Control-Allow-Headers: Content-Type\r\n"
                                "Access-Control-Max-Age: 86400\r\n"
                                "Connection: close\r\n\r\n";
                            send(new_socket, preflightResponse, strlen(preflightResponse), 0);
                            close(new_socket);
                            handled = true;
                        }
                        else if (request.find("/manual-install") != std::string::npos)
                        {
                            try {
                                LOG_INFO("StoreInject: Received /manual-install request\n");
                                std::string respBody = handleManualInstall(request);
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (const std::exception& e) {
                                LOG_WARN("StoreInject: Exception during manual-install: %s\n", e.what());
                                std::string respBody = "{\"success\":false,\"message\":\"Internal server error: " + std::string(e.what()) + "\"}";
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                std::string respBody = "{\"success\":false,\"message\":\"Unknown internal server error.\"}";
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/check?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /check for AppID %u\n", appId);
                                    
                                    bool gameExists = Apps::gameFilesExist(appId);
                                    bool luaExists = false;
                                    bool isUnlocked = g_config.isAddedAppId(appId);

                                    std::string pluginDir = g_config.getPluginDir();
                                    if (!pluginDir.empty()) {
                                        auto luaPath = std::filesystem::path(pluginDir) / (idStr + ".lua");
                                        if (std::filesystem::exists(luaPath)) {
                                            luaExists = true;
                                        }
                                    }

                                    bool exists = isUnlocked && (gameExists || luaExists);
                                    bool pending = g_pendingRestartApps.count(appId) > 0;
                                    bool onlineFixInstalled = Apps::isOnlineFixInstalled(appId);
                                    bool autoCrackInstalled = Apps::isAutoCrackInstalled(appId);
                                    
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    response += "{\"exists\":" + std::string(exists ? "true" : "false") + 
                                               ",\"pending\":" + std::string(pending ? "true" : "false") +
                                               ",\"onlineFixInstalled\":" + std::string(onlineFixInstalled ? "true" : "false") +
                                               ",\"autoCrackInstalled\":" + std::string(autoCrackInstalled ? "true" : "false") + "}";
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/verify-files?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /verify-files for AppID %u\n", appId);
                                    
                                    const char* home = getenv("HOME");
                                    if (home) {
                                        std::string scriptPath = std::string(home) + "/.local/share/ACCELA/scripts/accela-download.sh";
                                        std::string cmd = "SCRIPT=\"" + scriptPath + "\"; APPID=\"" + std::to_string(appId) + "\"; "
                                                          "for term in wezterm konsole gnome-terminal ptyxis alacritty tilix xfce4-terminal terminator mate-terminal lxterminal xterm kitty; do "
                                                          "if command -v $term >/dev/null 2>&1; then "
                                                          "case $term in "
                                                          "wezterm) exec wezterm start --always-new-process -- \"$SCRIPT\" \"$APPID\" ;; "
                                                          "gnome-terminal|ptyxis) exec $term -- \"$SCRIPT\" \"$APPID\" ;; "
                                                          "*) exec $term -e \"$SCRIPT\" \"$APPID\" ;; "
                                                          "esac; "
                                                          "fi; "
                                                          "done";
                                        
                                        pid_t pid = fork();
                                        if (pid == 0) {
                                            setsid();
                                            for (int fd = 3; fd < 1024; fd++) close(fd);
                                            unsetenv("LD_PRELOAD");
                                            unsetenv("LD_AUDIT");
                                            freopen("/dev/null", "r", stdin);
                                            freopen("/dev/null", "w", stdout);
                                            freopen("/dev/null", "w", stderr);
                                            execl("/bin/bash", "bash", "-c", cmd.c_str(), nullptr);
                                            _exit(1);
                                        } else if (pid > 0) {
                                            LOG_INFO("StoreInject: Started verify-files terminal child with PID %d\n", pid);
                                        } else {
                                            LOG_WARN("StoreInject: fork() failed for verify-files: %s\n", strerror(errno));
                                        }
                                    }
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/install-fix?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /install-fix for AppID %u\n", appId);
                                    
                                    Apps::setOnlineFixInstalled(appId, true);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove-fix?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /remove-fix for AppID %u\n", appId);
                                    
                                    Apps::setOnlineFixInstalled(appId, false);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/install-crack?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /install-crack for AppID %u\n", appId);
                                    
                                    Apps::setAutoCrackInstalled(appId, true);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove-crack?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /remove-crack for AppID %u\n", appId);
                                    
                                    Apps::setAutoCrackInstalled(appId, false);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/fix-install?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /fix-install for AppID %u\n", appId);
                                    
                                    Apps::removeInstalled(appId);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove?id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find_first_of(" &", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                uint32_t appId = std::stoul(idStr);
                                g_pendingRestartApps.insert(appId);
                                
                                bool deleteGame = (request.find("game=true") != std::string::npos);
                                
                                if (deleteGame) {
                                    Apps::deleteGameFiles(appId);
                                }
                                
                                // Remove Lua and Manifest files
                                std::string pluginDir = g_config.getPluginDir();
                                if (!pluginDir.empty()) {
                                    LOG_INFO("RemoveLua: Scanning %s for AppID %u\n", pluginDir.c_str(), appId);
                                    
                                    // 1. Smart Lua Deletion: Scan file contents for addappid(ID)
                                    try {
                                        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                                                std::ifstream file(entry.path());
                                                if (file.is_open()) {
                                                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                                                    file.close();
                                                    
                                                    // Look for addappid(appId) or addappid( appId )
                                                    std::string pattern = "addappid(" + idStr + ")";
                                                    if (content.find("addappid") != std::string::npos && content.find(idStr) != std::string::npos) {
                                                        std::filesystem::remove(entry.path());
                                                        LOG_INFO("RemoveLua: Deleted lua plugin %s (contained AppID %u)\n", entry.path().c_str(), appId);
                                                    }
                                                }
                                            }
                                        }
                                    } catch(const std::exception& e) { LOG_WARN("RemoveLua: Error scanning plugins: %s\n", e.what()); }
                                    
                                    // 2. Remove manifests from pluginDir
                                    try {
                                        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                            if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                                                std::string stem = entry.path().stem().string();
                                                if (stem.find(idStr) == 0) {
                                                    std::filesystem::remove(entry.path());
                                                    LOG_INFO("RemoveLua: Deleted manifest %s from pluginDir\n", entry.path().c_str());
                                                }
                                            }
                                        }
                                    } catch(...) {}

                                    // 3. Remove manifests from depotcache
                                    std::filesystem::path configPath = std::filesystem::path(pluginDir).parent_path();
                                    std::filesystem::path depotcachePath = configPath / "depotcache";
                                    if (std::filesystem::exists(depotcachePath)) {
                                        try {
                                            for (const auto& entry : std::filesystem::directory_iterator(depotcachePath)) {
                                                if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                                                    std::string stem = entry.path().stem().string();
                                                    if (stem.find(idStr) == 0) {
                                                        std::filesystem::remove(entry.path());
                                                        LOG_INFO("RemoveLua: Deleted manifest %s from depotcache\n", entry.path().c_str());
                                                    }
                                                }
                                            }
                                        } catch(...) {}
                                    }
                                }

                                // 4. Finalize state and refresh memory
                                scanLuaPluginsAndUpdateConfig();
                                g_config.loadSettings();
                                

                                Apps::removeInstalled(appId); // Ensure it's removed from persistence too
                                g_config.removeAdditionalAppId(appId);
                                
                                // if (g_pClientApps) {
                                //     LOG_INFO("Triggering RequestAppInfoUpdate for %u\n", appId);
                                //     typedef void (*RequestAppInfoUpdate_t)(void*, uint32_t*, uint32_t, bool);
                                //     void** vtable = *reinterpret_cast<void***>(g_pClientApps);
                                //     RequestAppInfoUpdate_t requestUpdateFn = reinterpret_cast<RequestAppInfoUpdate_t>(vtable[7]);
                                //     uint32_t appIdArray[1] = { appId };
                                //     requestUpdateFn(g_pClientApps, appIdArray, 1, true);
                                // }
                                
                                const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                send(new_socket, response, strlen(response), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/restart") != std::string::npos)
                        {
                            LOG_INFO("Restart Steam requested via UI\n");
                            
                            // Send response first
                            const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                            send(new_socket, response, strlen(response), 0);
                            close(new_socket);
                            handled = true;
                            
                            // Preserve LD_AUDIT from current environment
                            const char* ldAudit = getenv("LD_AUDIT");
                            std::string ldAuditStr;
                            if (ldAudit && ldAudit[0] != '\0') {
                                ldAuditStr = ldAudit;
                            } else {
                                const char* home = getenv("HOME");
                                if (home) {
                                    ldAuditStr = std::string(home) + "/.local/share/SLSsteam/library-inject.so:"
                                               + std::string(home) + "/.local/share/SLSsteam/SLSsteam.so";
                                }
                            }
                            
                            // Find the real Steam binary
                            std::string steamBin;
                            for (const char* candidate : {"/usr/games/steam", "/usr/bin/steam", "/usr/local/bin/steam"}) {
                                if (std::filesystem::exists(candidate)) {
                                    steamBin = candidate;
                                    break;
                                }
                            }
                            if (steamBin.empty()) steamBin = "steam";
                            
                            // Build a restart script that:
                            // 1. Uses "steam -shutdown" for clean shutdown (like job_queue_manager.py)
                            // 2. Targets specific Steam processes by exact name, NOT "pkill -f steam"
                            //    which would also kill antigravity and anything with "steam" in its path
                            // 3. Waits for processes to die
                            // 4. Relaunches with LD_AUDIT (tier0 hook injects CDP pipe flags)
                            std::string script =
                                // Step 1: Ask Steam to shut down cleanly
                                "steam -shutdown 2>/dev/null; "
                                // Step 2: Wait for graceful shutdown
                                "sleep 3; "
                                // Step 3: Kill specific Steam processes if still alive (exact name match, not -f)
                                "pkill -TERM -x steam 2>/dev/null; "
                                "pkill -TERM -x steamwebhelper 2>/dev/null; "
                                "pkill -TERM -x steam-runtime-l 2>/dev/null; "
                                "sleep 2; "
                                // Step 4: Force-kill stragglers by exact name
                                "pkill -9 -x steam 2>/dev/null; "
                                "pkill -9 -x steamwebhelper 2>/dev/null; "
                                "sleep 1; "
                                // Step 5: Relaunch with LD_AUDIT (CDP pipe injection handled by tier0 hook)
                                "env";
                            if (!ldAuditStr.empty()) {
                                script += " LD_AUDIT=\"" + ldAuditStr + "\"";
                            }
                            script += " " + steamBin
                                   + " </dev/null >/dev/null 2>&1 &";
                            
                            LOG_INFO("Restart script: %s\n", script.c_str());
                            
                            // Use fork()+exec() to fully detach the restart process.
                            // system() blocks and the script kills our parent, causing issues.
                            // fork() creates a child that survives our death.
                            pid_t pid = fork();
                            if (pid == 0) {
                                // Child process: detach from parent completely
                                setsid();  // New session leader
                                // Close inherited file descriptors
                                for (int fd = 3; fd < 1024; fd++) close(fd);
                                // Redirect stdin/stdout/stderr to /dev/null
                                freopen("/dev/null", "r", stdin);
                                freopen("/dev/null", "w", stdout);
                                freopen("/dev/null", "w", stderr);
                                // Execute the restart script
                                execl("/bin/bash", "bash", "-c", script.c_str(), nullptr);
                                _exit(1);  // Only reached if execl fails
                            } else if (pid > 0) {
                                LOG_INFO("Restart child spawned with PID %d\n", pid);
                            } else {
                                LOG_WARN("fork() failed for restart: %s\n", strerror(errno));
                            }
                        }
                        else if (request.find("/log?msg=") != std::string::npos)
                        {
                            size_t msgPos = request.find("msg=");
                            size_t endPos = request.find(" ", msgPos);
                            if (endPos != std::string::npos)
                            {
                                std::string msg = request.substr(msgPos + 4, endPos - (msgPos + 4));
                                // Simple URL decode
                                std::replace(msg.begin(), msg.end(), '+', ' ');
                                LOG_INFO("StoreInject JS: %s\n", msg.c_str());
                            }
                        }
                        else if (request.find("id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find(" ", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                LOG_INFO("Download Lua clicked for Product ID: %s\n", idStr.c_str());
                            }
                        }
                    }

                    if (!handled)
                    {
                        const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                        send(new_socket, response, strlen(response), 0);
                        close(new_socket);
                    }
                }
            }
        }
        close(server_fd);
    }
    // patchIndexHtml removed.

    void init()
    {
        g_shouldStop = false;
        
        // Start the callback server immediately as it's just a passive listener
        g_workerThread = std::thread(callbackServer);
        
        // Delay the automation worker by 3 seconds to let Steam initialize safely
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (!g_shouldStop) {
                g_autoThread = std::thread(automationWorker);
                g_autoThread.detach(); 
            }
        }).detach();
    }

    void removeStoreInjectScript()
    {
        auto indexPath = findSteamUIIndexHtml();
        if (indexPath.empty()) return;

        std::ifstream inFile(indexPath);
        if (!inFile.is_open()) return;

        std::stringstream buffer;
        buffer << inFile.rdbuf();
        std::string content = buffer.str();
        inFile.close();

        auto beginPos = content.find(INJECT_MARKER_BEGIN);
        if (beginPos == std::string::npos) return;

        auto endPos = content.find(INJECT_MARKER_END);
        if (endPos == std::string::npos) return;

        auto removeEnd = endPos + strlen(INJECT_MARKER_END);
        if (removeEnd < content.size() && content[removeEnd] == '\n')
            removeEnd++;

        content.erase(beginPos, removeEnd - beginPos);

        std::ofstream outFile(indexPath, std::ios::trunc);
        if (outFile.is_open())
        {
            outFile << content;
            LOG_INFO("StoreInject: Removed injected script from index.html\n");
        }
    }

    void shutdown()
    {
        g_shouldStop = true;
        removeStoreInjectScript();
        if (g_workerThread.joinable())
        {
            g_workerThread.join();
        }
    }
}
