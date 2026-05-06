#include "storeinject.hpp"
#include "cdpinject.hpp"
#include "luadownload.hpp"
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace StoreInject
{
    static std::thread g_workerThread;
    static std::thread g_autoThread;
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
                                            g_pLog->info("LuaDownload: Completed for appid=%s\n", pid.c_str());
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
                            size_t rEnd = response.find("-TS=", rStart);
                            if (rEnd != std::string::npos)
                            {
                                size_t tsEnd = response.find_first_of("\"", rEnd + 4);
                                if (tsEnd != std::string::npos)
                                {
                                    std::string morr = urlDecode(response.substr(mStart, mEnd - mStart));
                                    std::string ryuu = urlDecode(response.substr(rStart, rEnd - rStart));
                                    std::string timestamp = response.substr(rEnd + 4, tsEnd - (rEnd + 4));

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
                        }
                        searchPos = mStart;
                    }

                    // Trigger the C++ CDP injector every 5 seconds to inject new pages
                    if (checkCounter++ % 2 == 0)
                    {
                        CDPInject::injectStorePages();
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
                    if (valread > 0)
                    {
                        buffer[valread] = '\0';
                        std::string request(buffer);
                        
                        if (request.find("/log?msg=") != std::string::npos)
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

                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                    send(new_socket, response, strlen(response), 0);
                    close(new_socket);
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
        
        // Delay the automation worker by 10 seconds to let Steam initialize safely
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(10));
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
