#include "cdpinject.hpp"
#include "../log.hpp"
#include "../config.hpp"
#include "apps.hpp"

#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include <openssl/sha.h>
#include <base64/base64.hpp>

namespace CDPInject
{
    // ── Minimal JSON helpers (no dependency needed for this simple structure) ──

    /**
     * Extract a string value for a given key from a JSON-ish string.
     * Only handles flat objects with string values — sufficient for CDP /json.
     */
    static std::string jsonGetString(const std::string& json, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) return {};

        // Skip past the key, colon, and opening quote
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return {};
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return {};
        pos++; // skip the opening quote

        auto end = json.find('"', pos);
        if (end == std::string::npos) return {};

        return json.substr(pos, end - pos);
    }

    static int jsonGetInt(const std::string& json, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) return -1;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return -1;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        if (pos >= json.size() || (!isdigit(json[pos]) && json[pos] != '-')) return -1;
        try { return std::stoi(json.substr(pos)); } catch (...) { return -1; }
    }


    /**
     * Split a JSON array of objects into individual object strings.
     */
    static std::vector<std::string> jsonSplitArray(const std::string& json)
    {
        std::vector<std::string> objects;
        int depth = 0;
        size_t objStart = 0;
        bool inObj = false;

        for (size_t i = 0; i < json.size(); i++)
        {
            if (json[i] == '{')
            {
                if (depth == 0) { objStart = i; inObj = true; }
                depth++;
            }
            else if (json[i] == '}')
            {
                depth--;
                if (depth == 0 && inObj)
                {
                    objects.push_back(json.substr(objStart, i - objStart + 1));
                    inObj = false;
                }
            }
        }
        return objects;
    }

    // ── TCP helper ──

    /**
     * Connect to host:port with a timeout. Returns socket fd or -1.
     */
    static int tcpConnect(const char* host, int port, int timeoutSec = 2)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);

        struct timeval tv;
        tv.tv_sec = timeoutSec;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        {
            close(sock);
            return -1;
        }
        return sock;
    }

    /**
     * Read all available data from a socket until EOF or timeout.
     */
    static std::string tcpReadAll(int sock)
    {
        std::string result;
        char buf[4096];
        int n;
        while ((n = read(sock, buf, sizeof(buf) - 1)) > 0)
        {
            buf[n] = '\0';
            result += buf;
        }
        return result;
    }

    // ── WebSocket client (RFC 6455, minimal for CDP) ──

    /**
     * Parse a ws:// URL into host, port, path.
     * Expected format: ws://host:port/path
     */
    static bool parseWsUrl(const std::string& wsUrl, std::string& host, int& port, std::string& path)
    {
        // ws://127.0.0.1:8080/devtools/page/XXXX
        if (wsUrl.rfind("ws://", 0) != 0) return false;

        size_t hostStart = 5; // after "ws://"
        size_t colonPos = wsUrl.find(':', hostStart);
        if (colonPos == std::string::npos) return false;

        host = wsUrl.substr(hostStart, colonPos - hostStart);

        size_t slashPos = wsUrl.find('/', colonPos);
        if (slashPos == std::string::npos)
        {
            port = std::stoi(wsUrl.substr(colonPos + 1));
            path = "/";
        }
        else
        {
            port = std::stoi(wsUrl.substr(colonPos + 1, slashPos - colonPos - 1));
            path = wsUrl.substr(slashPos);
        }
        return true;
    }

    /**
     * Generate a random 16-byte WebSocket key, base64-encoded.
     */
    static std::string generateWsKey()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dist(0, 255);

        char raw[16];
        for (int i = 0; i < 16; i++) raw[i] = static_cast<char>(dist(gen));

        return base64::to_base64(std::string_view(raw, 16));
    }

    /**
     * Perform the WebSocket upgrade handshake.
     * Returns true if the server accepted (HTTP 101).
     */
    static bool wsHandshake(int sock, const std::string& host, int port, const std::string& path, const std::string& wsKey)
    {
        std::ostringstream req;
        req << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << port << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << wsKey << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n"
            << "\r\n";

        std::string reqStr = req.str();
        if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) return false;

        // Read the HTTP response (just need to see "101")
        char buf[2048];
        int n = read(sock, buf, sizeof(buf) - 1);
        if (n <= 0) return false;
        buf[n] = '\0';

        return std::string(buf, n).find("101") != std::string::npos;
    }

    /**
     * Send a WebSocket text frame (masked, as required by RFC 6455 for clients).
     */
    static bool wsSendText(int sock, const std::string& payload)
    {
        std::vector<uint8_t> frame;

        // Opcode 0x1 = text, FIN bit set
        frame.push_back(0x81);

        // Payload length + mask bit (0x80)
        size_t len = payload.size();
        if (len <= 125)
        {
            frame.push_back(static_cast<uint8_t>(len | 0x80));
        }
        else if (len <= 65535)
        {
            frame.push_back(0xFE); // 126 | 0x80
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        }
        else
        {
            frame.push_back(0xFF); // 127 | 0x80
            for (int i = 7; i >= 0; i--)
                frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
        }

        // Masking key (4 random bytes)
        std::random_device rd;
        uint8_t mask[4];
        for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(rd());
        frame.insert(frame.end(), mask, mask + 4);

        // Masked payload
        for (size_t i = 0; i < len; i++)
            frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);

        ssize_t sent = send(sock, frame.data(), frame.size(), 0);
        return sent == static_cast<ssize_t>(frame.size());
    }

    /**
     * Receive a WebSocket frame and return the payload.
     * Only handles text/binary frames up to ~64KB (sufficient for CDP responses).
     */
    static std::string wsRecvFrame(int sock)
    {
        uint8_t header[2];
        if (read(sock, header, 2) != 2) return {};

        bool masked = (header[1] & 0x80) != 0;
        uint64_t payloadLen = header[1] & 0x7F;

        if (payloadLen == 126)
        {
            uint8_t ext[2];
            if (read(sock, ext, 2) != 2) return {};
            payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        }
        else if (payloadLen == 127)
        {
            uint8_t ext[8];
            if (read(sock, ext, 8) != 8) return {};
            payloadLen = 0;
            for (int i = 0; i < 8; i++)
                payloadLen = (payloadLen << 8) | ext[i];
        }

        uint8_t maskKey[4] = {};
        if (masked)
        {
            if (read(sock, maskKey, 4) != 4) return {};
        }

        // Read payload in chunks
        std::string payload;
        payload.resize(payloadLen);
        size_t totalRead = 0;
        while (totalRead < payloadLen)
        {
            ssize_t n = read(sock, &payload[totalRead], payloadLen - totalRead);
            if (n <= 0) break;
            totalRead += n;
        }

        if (masked)
        {
            for (size_t i = 0; i < payload.size(); i++)
                payload[i] ^= maskKey[i % 4];
        }

        return payload;
    }

    // ── Public API ──

    std::vector<CDPPage> fetchPages(const char* host, int port)
    {
        std::vector<CDPPage> pages;

        int sock = tcpConnect(host, port);
        if (sock < 0) return pages;

        // Send HTTP GET /json
        std::string httpReq = "GET /json HTTP/1.1\r\nHost: ";
        httpReq += host;
        httpReq += ":";
        httpReq += std::to_string(port);
        httpReq += "\r\nConnection: close\r\n\r\n";
        send(sock, httpReq.c_str(), httpReq.size(), 0);

        std::string response = tcpReadAll(sock);
        close(sock);

        // Strip HTTP headers — find the JSON array start
        auto bodyStart = response.find("\r\n\r\n");
        if (bodyStart == std::string::npos) return pages;
        std::string body = response.substr(bodyStart + 4);

        // Handle chunked transfer encoding — strip chunk headers
        // Simple approach: find the first '[' and last ']'
        auto arrStart = body.find('[');
        auto arrEnd = body.rfind(']');
        if (arrStart == std::string::npos || arrEnd == std::string::npos) return pages;
        std::string jsonArray = body.substr(arrStart, arrEnd - arrStart + 1);

        auto objects = jsonSplitArray(jsonArray);
        for (auto& obj : objects)
        {
            CDPPage page;
            page.url = jsonGetString(obj, "url");
            page.title = jsonGetString(obj, "title");
            page.webSocketDebuggerUrl = jsonGetString(obj, "webSocketDebuggerUrl");
            pages.push_back(std::move(page));
        }

        return pages;
    }

    bool injectJS(const std::string& wsUrl, const std::string& jsCode)
    {
        // WebSocket-based injection via --remote-debugging-port=8080
        // The tier0 hook injects this flag into steamwebhelper only,
        // so we don't need --cef-enable-debugging on the main Steam process
        std::string host;
        int port = 0;
        std::string path;

        if (!parseWsUrl(wsUrl, host, port, path))
        {
            g_pLog->info("CDPInject: Failed to parse WS URL: %s\n", wsUrl.c_str());
            return false;
        }

        int sock = tcpConnect(host.c_str(), port);
        if (sock < 0)
        {
            g_pLog->info("CDPInject: Failed to connect to %s:%d\n", host.c_str(), port);
            return false;
        }

        std::string wsKey = generateWsKey();
        if (!wsHandshake(sock, host, port, path, wsKey))
        {
            g_pLog->info("CDPInject: WebSocket handshake failed for %s\n", wsUrl.c_str());
            close(sock);
            return false;
        }

        // Build CDP Runtime.evaluate JSON payload
        // We need to escape the JS code for JSON embedding
        std::string escapedJs;
        escapedJs.reserve(jsCode.size() + 64);
        for (char c : jsCode)
        {
            switch (c)
            {
                case '"':  escapedJs += "\\\""; break;
                case '\\': escapedJs += "\\\\"; break;
                case '\n': escapedJs += "\\n"; break;
                case '\r': escapedJs += "\\r"; break;
                case '\t': escapedJs += "\\t"; break;
                default:   escapedJs += c; break;
            }
        }

        std::string cdpPayload = R"({"id":1,"method":"Runtime.evaluate","params":{"expression":")" + escapedJs + R"(","userGesture":true,"awaitPromise":true}})";

        if (!wsSendText(sock, cdpPayload))
        {
            g_pLog->info("CDPInject: Failed to send CDP payload to %s\n", wsUrl.c_str());
            close(sock);
            return false;
        }

        // Read the response (we don't really need it, but consume it to be clean)
        std::string response = wsRecvFrame(sock);
        (void)response;

        g_pLog->info("CDPInject: Successfully injected JS into target WebSocket: %s\n", wsUrl.c_str());

        close(sock);
        return true;
    }

    /**
     * Helper: send a CDP command and read frames until we get the response
     * with the matching ID, or timeout after maxFrames attempts.
     */
    static std::string cdpSendAndRecv(int sock, int id, const std::string& payload, int maxFrames = 30)
    {
        if (!wsSendText(sock, payload)) return {};

        std::string idStr = "\"id\":" + std::to_string(id);
        for (int i = 0; i < maxFrames; i++)
        {
            std::string frame = wsRecvFrame(sock);
            if (frame.empty()) break;
            if (frame.find(idStr) != std::string::npos)
                return frame;
        }
        return {};
    }

    int downloadViaPage(const std::string& url, const std::string& destPath)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(40000, 50000);
        int port = dist(gen);
        std::string portArg = "--remote-debugging-port=" + std::to_string(port);

        g_pLog->info("CDPInject::downloadViaPage: Spawning headless browser proxy on port %d\n", port);

        pid_t pid = fork();
        if (pid < 0) {
            g_pLog->info("CDPInject::downloadViaPage: fork failed\n");
            return -1;
        }

        if (pid == 0) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            freopen("/dev/null", "r", stdin);

            // Unique user-data-dir avoids colliding with existing Chrome/Steam instances
            std::string userDataDir = "--user-data-dir=/tmp/sls_headless_proxy_" + std::to_string(port);

            // First try Steam's built-in browser (steamwebhelper) so the user doesn't need Chrome installed
            const char* home = getenv("HOME");
            if (home) {
                std::string path1 = std::string(home) + "/.steam/steam/ubuntu12_64/steamwebhelper";
                std::string path2 = std::string(home) + "/.local/share/Steam/ubuntu12_64/steamwebhelper";
                std::string path3 = std::string(home) + "/.var/app/com.valvesoftware.Steam/data/Steam/ubuntu12_64/steamwebhelper";

                execl(path1.c_str(), "steamwebhelper", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
                execl(path2.c_str(), "steamwebhelper", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
                execl(path3.c_str(), "steamwebhelper", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
            }

            // Fallback to system Chrome/Chromium if steamwebhelper fails
            execlp("google-chrome", "google-chrome", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
            execlp("google-chrome-stable", "google-chrome-stable", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
            execlp("chromium-browser", "chromium-browser", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
            execlp("chromium", "chromium", "--headless", "--disable-gpu", userDataDir.c_str(), portArg.c_str(), "about:blank", nullptr);
            _exit(1);
        }

        std::vector<CDPPage> pages;
        for (int i = 0; i < 20; i++) {
            pages = fetchPages("127.0.0.1", port);
            if (!pages.empty()) {
                break;
            }

            // Steam's steamwebhelper doesn't open a default tab in headless mode.
            // If the server is up but returned no pages, we must ask it to create one.
            int sock = tcpConnect("127.0.0.1", port);
            if (sock >= 0) {
                std::string httpReq = "PUT /json/new HTTP/1.1\r\nHost: 127.0.0.1:";
                httpReq += std::to_string(port);
                httpReq += "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                send(sock, httpReq.c_str(), httpReq.size(), 0);
                // We don't care about the response body, just triggering the creation
                close(sock);
            }

            usleep(250000); // Wait 0.25s each time, max 5 seconds
        }

        if (pages.empty()) {
            g_pLog->info("CDPInject::downloadViaPage: Failed to connect to headless proxy\n");
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            return -1;
        }

        std::string wsUrl = pages[0].webSocketDebuggerUrl;
        
        std::string host2; int port2 = 0; std::string path2;
        if (!parseWsUrl(wsUrl, host2, port2, path2)) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            return -1;
        }

        int sock = tcpConnect(host2.c_str(), port2, 30);
        if (sock < 0) { 
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            return -1; 
        }

        struct timeval tv{60, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string wsKey = generateWsKey();
        if (!wsHandshake(sock, host2, port2, path2, wsKey))
        {
            close(sock);
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            return -1;
        }

        // Step 7: Inject fetch() JS
        std::string safeUrl = url;
        for (size_t i = 0; i < safeUrl.size(); ++i)
        {
            if (safeUrl[i] == '\'' || safeUrl[i] == '\\') { safeUrl.insert(i, "\\"); ++i; }
        }

        std::string fetchJs =
            "(async function(){"
            "return await new Promise((resolve)=>{"
            "try{"
            "var xhr=new XMLHttpRequest();"
            "xhr.open('GET','" + safeUrl + "',true);"
            "xhr.responseType='arraybuffer';"
            "xhr.onload=function(){"
            "try{"
            "var u=new Uint8Array(xhr.response);"
            "var c='';"
            "var K=8192;"
            "for(var i=0;i<u.length;i+=K){"
            "c+=String.fromCharCode.apply(null,u.subarray(i,Math.min(i+K,u.length)));"
            "}"
            "resolve('OK:'+xhr.status+':'+btoa(c));"
            "}catch(e){"
            "resolve('ERR:-2:'+e.message);"
            "}"
            "};"
            "xhr.onerror=function(){"
            "resolve('ERR:-1:xhr failed');"
            "};"
            "xhr.send();"
            "}catch(e){"
            "resolve('ERR:-3:'+e.message);"
            "}"
            "});"
            "})()";

        std::string escapedJs;
        escapedJs.reserve(fetchJs.size() + 64);
        for (char c : fetchJs)
        {
            switch (c)
            {
                case '"':  escapedJs += "\\\""; break;
                case '\\': escapedJs += "\\\\"; break;
                case '\n': escapedJs += "\\n";  break;
                case '\r': escapedJs += "\\r";  break;
                case '\t': escapedJs += "\\t";  break;
                default:   escapedJs += c;      break;
            }
        }

        std::string evalPayload = "{\"id\":11,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"" + escapedJs + "\",\"awaitPromise\":true,\"returnByValue\":true}}";

        g_pLog->info("CDPInject::downloadViaPage: Fetching file...\n");
        std::string response = cdpSendAndRecv(sock, 11, evalPayload, 60);

        close(sock);
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);

        if (response.empty()) { g_pLog->info("CDPInject::downloadViaPage: No response\n"); return -1; }

        std::string value = jsonGetString(response, "value");
        if (value.empty()) { g_pLog->info("CDPInject::downloadViaPage: Empty value: %.200s\n", response.c_str()); return -1; }

        if (value.rfind("ERR:", 0) == 0)
        {
            int errStatus = -1;
            try { errStatus = std::stoi(value.substr(4)); } catch (...) {}
            g_pLog->info("CDPInject::downloadViaPage: %s\n", value.c_str());
            return errStatus;
        }
        if (value.rfind("OK:", 0) != 0) { g_pLog->info("CDPInject::downloadViaPage: Unexpected: %.100s\n", value.c_str()); return -1; }

        size_t firstColon = value.find(':', 3);
        if (firstColon == std::string::npos) return -1;

        int httpStatus = 200;
        try { httpStatus = std::stoi(value.substr(3, firstColon - 3)); } catch (...) {}

        std::string b64data = value.substr(firstColon + 1);
        if (b64data.empty()) { g_pLog->info("CDPInject::downloadViaPage: Empty data (HTTP %d)\n", httpStatus); return httpStatus; }

        std::string decoded = base64::from_base64(b64data);
        FILE* fp = fopen(destPath.c_str(), "wb");
        if (!fp) { g_pLog->info("CDPInject::downloadViaPage: Cannot open %s\n", destPath.c_str()); return -1; }
        fwrite(decoded.data(), 1, decoded.size(), fp);
        fclose(fp);

        g_pLog->info("CDPInject::downloadViaPage: Downloaded %zu bytes (HTTP %d) -> %s\n",
                     decoded.size(), httpStatus, destPath.c_str());
        return httpStatus;
    }

    std::string loadResourceFile(const std::string& filename)
    {
        std::vector<std::string> paths;

        // 1. Try to resolve relative to SLSsteam.so location using dladdr
        Dl_info info;
        if (dladdr((void*)loadResourceFile, &info) && info.dli_fname)
        {
            try
            {
                std::filesystem::path libPath(info.dli_fname);
                std::filesystem::path libDir = libPath.parent_path();
                paths.push_back((libDir / filename).string());
                paths.push_back((libDir / "res" / "inject-scripts" / filename).string());
                paths.push_back((libDir / ".." / "res" / "inject-scripts" / filename).string());
            }
            catch (...) {}
        }

        // 2. Add current working directory candidates
        paths.push_back(filename);
        paths.push_back("./res/inject-scripts/" + filename);
        paths.push_back("../res/inject-scripts/" + filename);

        // 3. Add home folder ~/.local/share/SLSsteam candidates
        const char* home = getenv("HOME");
        if (home)
        {
            paths.push_back(std::string(home) + "/.local/share/SLSsteam/" + filename);
            paths.push_back(std::string(home) + "/.local/share/SLSsteam/res/inject-scripts/" + filename);
        }

        for (const auto& path : paths) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                g_pLog->info("CDPInject::loadResourceFile: Successfully loaded %s from %s\n", filename.c_str(), path.c_str());
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }

        g_pLog->info("CDPInject::loadResourceFile: Could not find %s in any candidate path!\n", filename.c_str());
        return "";
    }

    bool isScriptInjected(const std::string& wsUrl, const std::string& checkExpression)
    {
        std::string host;
        int port = 0;
        std::string path;

        if (!parseWsUrl(wsUrl, host, port, path))
        {
            g_pLog->info("CDPInject: Failed to parse WS URL for check: %s\n", wsUrl.c_str());
            return false;
        }

        int sock = tcpConnect(host.c_str(), port);
        if (sock < 0)
        {
            return false;
        }

        std::string wsKey = generateWsKey();
        if (!wsHandshake(sock, host, port, path, wsKey))
        {
            close(sock);
            return false;
        }

        std::string checkPayload = R"({"id":777,"method":"Runtime.evaluate","params":{"expression":")" + checkExpression + R"(","returnByValue":true}})";
        if (!wsSendText(sock, checkPayload))
        {
            close(sock);
            return false;
        }

        std::string response = wsRecvFrame(sock);
        close(sock);

        if (response.find("\"value\":true") != std::string::npos)
        {
            return true;
        }

        return false;
    }

    void injectStorePages()
    {
        auto pages = fetchPages();
        if (pages.empty()) return;

        std::vector<std::string> pagesToInject;
        for (auto& page : pages)
        {
            if (page.url.find("store.steampowered.com") != std::string::npos && !page.webSocketDebuggerUrl.empty())
            {
                if (!isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsLuaBtnAdded"))
                {
                    pagesToInject.push_back(page.webSocketDebuggerUrl);
                }
            }
        }

        if (pagesToInject.empty()) return;

        // Lazy-loaded cache
        static std::string cachedStorePageScript;
        if (cachedStorePageScript.empty())
        {
            std::string storePageScript = loadResourceFile("store-page-script.js");
            if (storePageScript.empty()) {
                g_pLog->debug("CDPInject::injectStorePages: Failed to load store-page-script.js\n");
                return;
            }

            std::string morrKey = g_config.morrenusKey.get();
            std::string ryuuKey = g_config.ryuuKey.get();

            size_t pos;
            if ((pos = storePageScript.find("%MORR_KEY%")) != std::string::npos)
                storePageScript.replace(pos, 10, morrKey);
            if ((pos = storePageScript.find("%RYUU_KEY%")) != std::string::npos)
                storePageScript.replace(pos, 10, ryuuKey);

            cachedStorePageScript = std::move(storePageScript);
        }

        for (const auto& wsUrl : pagesToInject)
        {
            injectJS(wsUrl, cachedStorePageScript);
        }
    }
}
