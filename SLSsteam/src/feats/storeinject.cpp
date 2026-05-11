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
        g_pLog->info("StoreInject: Automation worker started\n");
        
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
                                    g_pLog->info("Remove Lua clicked for Product ID: %s\n", productId.c_str());

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
                                                    g_pLog->info("RemoveLua: Deleted lua file %s\n", luaPath.c_str());
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
                                                                g_pLog->info("RemoveLua: Deleted manifest %s from pluginDir\n", path.c_str());
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
                                                                    g_pLog->info("RemoveLua: Deleted manifest %s from depotcache\n", path.c_str());
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            
                                            // 3. Update Config
                                            g_config.removeAdditionalAppId(appId);
                                            
                                            g_pLog->info("RemoveLua: Completed for appid=%s\n", pid.c_str());
                                        } catch (const std::exception& e) {
                                            g_pLog->warn("RemoveLua: Error processing appid %s: %s\n", pid.c_str(), e.what());
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
                                    g_pLog->info("Download Lua clicked for Product ID: %s\n", productId.c_str());

                                    // Trigger download in background thread
                                    std::string pid = productId; // copy for lambda capture
                                    std::thread([pid]() {
                                        bool ok = LuaDownload::downloadAndInstall(pid);
                                        if (ok)
                                        {
                                            g_pLog->info("LuaDownload: Completed for appid=%s\n", pid.c_str());

                                            // Ensure the game shows in library even if the user
                                            // navigated away from the store page (which would
                                            // prevent the CDP steam://install/ from firing).
                                            try {
                                                uint32_t appId = std::stoul(pid);
                                                g_config.addAdditionalAppId(appId);
                                                Apps::setInstalled(appId);
                                                scanLuaPluginsAndUpdateConfig();
                                            } catch (...) {
                                                g_pLog->warn("LuaDownload: Failed to register appid=%s in library\n", pid.c_str());
                                            }
                                        }
                                        else
                                            g_pLog->info("LuaDownload: Failed for appid=%s\n", pid.c_str());
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
                                    g_pLog->info("API Settings received via CDP UI! Updating config...\n");
                                    if (!morr.empty()) g_config.morrenusKey = morr;
                                    if (!ryuu.empty()) g_config.ryuuKey = ryuu;
                                    g_config.updateApiAuth(g_config.morrenusKey.get(), g_config.ryuuKey.get());
                                }
                            }
                        }
                        searchPos = mStart;
                    }

                    // Trigger the C++ CDP injector every 5 seconds to inject new pages
                    if (checkCounter++ % 2 == 0)
                    {
                        CDPInject::injectStorePages();
                        RemoveLua::injectRemoveLuaScript();
                    }
                }
                close(sock);
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    static void callbackServer()
    {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);
        char buffer[2048] = {0};

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

        g_pLog->info("StoreInject: Callback server listening on port 9001\n");

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
                    int valread = read(new_socket, buffer, 2047);
                    bool handled = false;
                    if (valread > 0)
                    {
                        buffer[valread] = '\0';
                        std::string request(buffer);
                        
                        if (request.find("/check?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    g_pLog->info("StoreInject: Received /check for AppID %u\n", appId);
                                    
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
                                    
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    response += "{\"exists\":" + std::string(exists ? "true" : "false") + 
                                               ",\"pending\":" + std::string(pending ? "true" : "false") + "}";
                                    send(new_socket, response.c_str(), response.size(), 0);
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
                                    g_pLog->info("RemoveLua: Scanning %s for AppID %u\n", pluginDir.c_str(), appId);
                                    
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
                                                        g_pLog->info("RemoveLua: Deleted lua plugin %s (contained AppID %u)\n", entry.path().c_str(), appId);
                                                    }
                                                }
                                            }
                                        }
                                    } catch(const std::exception& e) { g_pLog->warn("RemoveLua: Error scanning plugins: %s\n", e.what()); }
                                    
                                    // 2. Remove manifests from pluginDir
                                    try {
                                        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                            if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                                                std::string stem = entry.path().stem().string();
                                                if (stem.find(idStr) == 0) {
                                                    std::filesystem::remove(entry.path());
                                                    g_pLog->info("RemoveLua: Deleted manifest %s from pluginDir\n", entry.path().c_str());
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
                                                        g_pLog->info("RemoveLua: Deleted manifest %s from depotcache\n", entry.path().c_str());
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
                                //     g_pLog->info("Triggering RequestAppInfoUpdate for %u\n", appId);
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
                            g_pLog->info("Restart Steam requested via UI\n");
                            
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
                            
                            g_pLog->info("Restart script: %s\n", script.c_str());
                            
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
                                g_pLog->info("Restart child spawned with PID %d\n", pid);
                            } else {
                                g_pLog->warn("fork() failed for restart: %s\n", strerror(errno));
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
                                g_pLog->info("StoreInject JS: %s\n", msg.c_str());
                            }
                        }
                        else if (request.find("id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find(" ", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                g_pLog->info("Download Lua clicked for Product ID: %s\n", idStr.c_str());
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
            g_pLog->info("StoreInject: Removed injected script from index.html\n");
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
