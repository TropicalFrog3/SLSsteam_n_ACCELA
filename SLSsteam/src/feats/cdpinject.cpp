#include "cdpinject.hpp"
#include "../log.hpp"
#include "../config.hpp"
#include "apps.hpp"

#include <cstring>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

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
            g_pLog->debug("CDPInject: Failed to parse WS URL: %s\n", wsUrl.c_str());
            return false;
        }

        int sock = tcpConnect(host.c_str(), port);
        if (sock < 0)
        {
            g_pLog->debug("CDPInject: Failed to connect to %s:%d\n", host.c_str(), port);
            return false;
        }

        std::string wsKey = generateWsKey();
        if (!wsHandshake(sock, host, port, path, wsKey))
        {
            g_pLog->debug("CDPInject: WebSocket handshake failed for %s\n", wsUrl.c_str());
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
            g_pLog->debug("CDPInject: Failed to send CDP payload to %s\n", wsUrl.c_str());
            close(sock);
            return false;
        }

        // Read the response (we don't really need it, but consume it to be clean)
        std::string response = wsRecvFrame(sock);
        (void)response;

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
        // Step 1: Snapshot existing pages + get CDP host/port
        auto pages = fetchPages();
        if (pages.empty()) { g_pLog->info("CDPInject::downloadViaPage: No CDP pages\n"); return -1; }

        std::string cdpHost; int cdpPort = 0; std::string tmp;
        if (!parseWsUrl(pages[0].webSocketDebuggerUrl, cdpHost, cdpPort, tmp)) return -1;

        std::set<std::string> existingWsUrls;
        for (auto& p : pages) existingWsUrls.insert(p.webSocketDebuggerUrl);

        // Step 2: Create new hidden target via PUT /json/new, capture targetId
        std::string newTargetId;
        {
            int ctrlSock = tcpConnect(cdpHost.c_str(), cdpPort, 10);
            if (ctrlSock < 0) { g_pLog->info("CDPInject::downloadViaPage: Cannot reach CDP\n"); return -1; }
            std::string req = "PUT /json/new?about:blank HTTP/1.1\r\nHost: " + cdpHost + ":" + std::to_string(cdpPort) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(ctrlSock, req.c_str(), req.size(), 0);
            struct timeval tv2{5, 0};
            setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, &tv2, sizeof(tv2));
            std::string resp = tcpReadAll(ctrlSock);
            close(ctrlSock);
            auto bodyPos = resp.find("\r\n\r\n");
            if (bodyPos != std::string::npos)
                newTargetId = jsonGetString(resp.substr(bodyPos + 4), "id");
            g_pLog->info("CDPInject::downloadViaPage: Created hidden target id=%s\n", newTargetId.c_str());
        }

        // Step 3: Find the new target's WS URL
        std::string popupWsUrl;
        for (int attempt = 0; attempt < 20 && popupWsUrl.empty(); ++attempt)
        {
            // usleep(300000);
            for (auto& pg : fetchPages())
            {
                if (!pg.webSocketDebuggerUrl.empty() &&
                    existingWsUrls.find(pg.webSocketDebuggerUrl) == existingWsUrls.end())
                {
                    popupWsUrl = pg.webSocketDebuggerUrl;
                    break;
                }
            }
        }
        if (popupWsUrl.empty()) { g_pLog->info("CDPInject::downloadViaPage: Target never appeared\n"); return -1; }
        g_pLog->info("CDPInject::downloadViaPage: Hidden target ready\n");

        // Step 4: Connect WebSocket to the hidden target
        std::string host2; int port2 = 0; std::string path2;
        if (!parseWsUrl(popupWsUrl, host2, port2, path2)) return -1;

        int sock = tcpConnect(host2.c_str(), port2, 30);
        if (sock < 0) { g_pLog->info("CDPInject::downloadViaPage: WS connect failed\n"); return -1; }

        struct timeval tv{60, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string wsKey = generateWsKey();
        if (!wsHandshake(sock, host2, port2, path2, wsKey))
        {
            g_pLog->info("CDPInject::downloadViaPage: WS handshake failed\n");
            close(sock); return -1;
        }

        // // Step 5: IMMEDIATELY minimize the window so the user never sees it
        // if (!newTargetId.empty())
        // {
        //     std::string getWinPayload = "{\"id\":20,\"method\":\"Browser.getWindowForTarget\","
        //                                "\"params\":{\"targetId\":\"" + newTargetId + "\"}}";
        //     std::string winResp = cdpSendAndRecv(sock, 20, getWinPayload, 5);
        //     int windowId = jsonGetInt(winResp, "windowId");
        //     if (windowId > 0)
        //     {
        //         std::string minPayload = "{\"id\":21,\"method\":\"Browser.setWindowBounds\","
        //             "\"params\":{\"windowId\":" + std::to_string(windowId) +
        //             ",\"bounds\":{\"windowState\":\"minimized\"}}}";
        //         cdpSendAndRecv(sock, 21, minPayload, 5);
        //         g_pLog->info("CDPInject::downloadViaPage: Window hidden (id=%d)\n", windowId);
        //     }
        // }

        // Step 6: Navigate hidden target to base domain for same-origin fetch
        // std::string baseUrl;
        // {
        //     size_t p = url.find("://");
        //     if (p != std::string::npos)
        //     {
        //         size_t end = url.find('/', p + 3);
        //         baseUrl = url.substr(0, end != std::string::npos ? end : url.size());
        //     }
        // }
        // if (baseUrl.empty()) { close(sock); return -1; }

        // g_pLog->info("CDPInject::downloadViaPage: Navigating hidden target (user cannot see this)\n");
        // {
        //     std::string navPayload = "{\"id\":10,\"method\":\"Page.navigate\",\"params\":{\"url\":\"" + baseUrl + "/\"}}";
        //     cdpSendAndRecv(sock, 10, navPayload);
        // }
        // sleep(6);

        // Step 7: Inject fetch() JS
        std::string safeUrl = url;
        for (size_t i = 0; i < safeUrl.size(); ++i)
        {
            if (safeUrl[i] == '\'' || safeUrl[i] == '\\') { safeUrl.insert(i, "\\"); ++i; }
        }

        std::string fetchJs =
            "(async function(){"
            "try{"
            "var r=await fetch('" + safeUrl + "');"
            "var s=r.status;"
            "if(!r.ok)return 'ERR:'+s+':http';"
            "var b=await r.arrayBuffer();"
            "var u=new Uint8Array(b);"
            "var c='';"
            "var K=8192;"
            "for(var i=0;i<u.length;i+=K){"
            "c+=String.fromCharCode.apply(null,u.subarray(i,Math.min(i+K,u.length)));"
            "}"
            "return 'OK:'+s+':'+btoa(c);"
            "}catch(e){"
            "return 'ERR:-1:'+e.message;"
            "}"
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

        // Step 8: Close hidden target
        wsSendText(sock, "{\"id\":12,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"window.close()\"}}");
        close(sock);

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

    void injectStorePages()
    {
        auto pages = fetchPages();
        if (pages.empty()) return;

        // The store page script — identical to the one from inject_cef.py
        std::string storePageScript = R"JSRAW(
(function() {
    if (window.__slsLuaBtnAdded) return;
    window.__slsLuaBtnAdded = true;
    console.log('[SLS] Store Page Script Triggered');

    function ping(msg) { console.log('[SLS] StorePage: ' + msg); }

    ping('Script active on: ' + window.location.href);

    var observer = null;
    var debounceTimer = null;

    // Cache of app unlock status: { appid: true/false }
    var appUnlockStatus = {};

    function setupDownloadButton(luaLink, luaBtn, productID) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Download Lua';
        luaLink.style.filter = 'hue-rotate(110deg) brightness(1.2)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            var clickSpan = luaLink.querySelector('span');
            if (clickSpan) clickSpan.innerText = 'Downloading...';
            luaLink.style.filter = 'hue-rotate(50deg) brightness(1.0)';
            luaLink.style.pointerEvents = 'none';
            luaLink.style.opacity = '0.8';
            luaBtn.dataset.slsAppid = productID;
            ping('Lua Click: ' + productID);
            window.location.hash = 'sls-click-' + productID + '-' + Date.now();
        };
    }

    function setupRemoveButton(luaLink, luaBtn, productID) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Remove Lua';
        luaLink.style.filter = 'hue-rotate(320deg) brightness(1.1)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();

            // Show confirmation modal
            if (document.getElementById('sls-remove-overlay')) return;
            var overlay = document.createElement('div');
            overlay.id = 'sls-remove-overlay';
            overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(5px);';
            overlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:400px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;text-align:center;">' +
                '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">Remove Lua</h2>' +
                '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Remove Lua and Game files for AppID <b>' + productID + '</b>?</p>' +
                '<div style="display:flex;justify-content:center;gap:10px;">' +
                    '<button id="sls-rm-cancel" style="background:transparent;border:1px solid #333640;color:#e8e9eb;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;">Cancel</button>' +
                    '<button id="sls-rm-confirm" style="background:#ff4d4d;border:none;color:#fff;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;box-shadow:0 4px 10px rgba(255,77,77,0.3);">Remove</button>' +
                '</div>' +
            '</div>';
            document.body.appendChild(overlay);

            document.getElementById('sls-rm-cancel').onclick = function() { overlay.remove(); };
            document.getElementById('sls-rm-confirm').onclick = function() {
                var confirmBtn = document.getElementById('sls-rm-confirm');
                confirmBtn.innerText = 'Processing...';
                confirmBtn.style.opacity = '0.5';
                confirmBtn.style.pointerEvents = 'none';

                ping('Remove Lua: ' + productID);
                window.location.hash = 'sls-click-removelua-' + productID + '-' + Date.now();

                // Update cached status
                appUnlockStatus[productID] = false;

                overlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:400px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;text-align:center;">' +
                    '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#5cb85c;">Removed Successfully!</h2>' +
                    '<p style="margin:0;font-size:13px;color:#8a8d96;">Restart Steam for changes to take effect.</p>' +
                '</div>';
                setTimeout(function() { overlay.remove(); }, 3000);

                // Switch button back to Download Lua
                setupDownloadButton(luaLink, luaBtn, productID);
            };
        };
    }

    function addButtons() {
        if (observer) observer.disconnect();
        var cartBtns = document.querySelectorAll('.btn_addtocart, .btn_add_to_cart');
        cartBtns.forEach(function(cartBtn) {
            if (cartBtn.dataset.slsProcessed) return;
            if (cartBtn.classList.contains('sls-lua-btn')) return;
            var link = cartBtn.querySelector('a');
            if (!link) return;
            var hrefLower = link.href.toLowerCase();
            if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) return;
            var productID = null;
            var match = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
            if (match) {
                productID = match[2];
            }
            if (productID) {
                cartBtn.dataset.slsProcessed = '1';
                var luaBtn = cartBtn.cloneNode(true);
                luaBtn.classList.remove('btn_addtocart');
                luaBtn.classList.remove('btn_add_to_cart');
                luaBtn.classList.add('sls-lua-btn');
                luaBtn.dataset.slsProcessed = '1';
                luaBtn.style.display = 'inline-block';
                luaBtn.style.marginLeft = '8px';
                var luaLink = luaBtn.querySelector('a');
                if (luaLink) {
                    luaLink.href = 'javascript:void(0)';
                    luaLink.removeAttribute('id');

                    // Default to Download Lua, then check if already unlocked
                    setupDownloadButton(luaLink, luaBtn, productID);

                    // Check unlock status via callback server
                    if (appUnlockStatus[productID] !== undefined) {
                        // Use cached status
                        if (appUnlockStatus[productID]) {
                            setupRemoveButton(luaLink, luaBtn, productID);
                        }
                    } else {
                        // Query the server
                        (function(ll, lb, pid) {
                            fetch('http://127.0.0.1:9001/check?id=' + pid)
                                .then(function(r) { return r.json(); })
                                .then(function(data) {
                                    var isUnlocked = data.exists || data.pending;
                                    appUnlockStatus[pid] = isUnlocked;
                                    if (isUnlocked) {
                                        setupRemoveButton(ll, lb, pid);
                                    }
                                })
                                .catch(function() {
                                    ping('Check failed for ' + pid + ', defaulting to Download');
                                });
                        })(luaLink, luaBtn, productID);
                    }
                }
                cartBtn.parentNode.insertBefore(luaBtn, cartBtn.nextSibling);
                // Settings button
                var setBtn = cartBtn.cloneNode(true);
                setBtn.classList.remove('btn_addtocart', 'btn_add_to_cart');
                setBtn.classList.add('sls-settings-btn');
                setBtn.dataset.slsProcessed = '1';
                setBtn.style.display = 'inline-block';
                setBtn.style.marginLeft = '4px';
                var setLink = setBtn.querySelector('a');
                if (setLink) {
                    setLink.href = 'javascript:void(0)';
                    setLink.removeAttribute('id');
                    var spanSet = setLink.querySelector('span');
                    if (spanSet) spanSet.innerText = 'Settings';
                    setLink.style.filter = 'hue-rotate(200deg) brightness(1.1)';
                    setLink.style.padding = '0 10px';
                    setLink.onclick = function(e) {
                        e.preventDefault();
                        e.stopPropagation();
                        openSlsSettings();
                    };
                }
                luaBtn.parentNode.insertBefore(setBtn, luaBtn.nextSibling);
            }
        });
        if (observer && document.body) observer.observe(document.body, { childList: true, subtree: true });
    }

    function openSlsSettings() {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(5px);';
        overlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:450px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;">' +
            '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">API Settings</h2>' +
            '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Configure your credentials for 3rd-party download APIs.</p>' +
            '<label style="display:flex;justify-content:space-between;margin-bottom:8px;font-size:12px;font-weight:500;color:#b4b6bc;">' +
            '<span>Morrenus API Key</span>' +
            '<a href="https://manifest.morrenus.xyz/api-keys/stats" target="_blank" style="color:#007bff;text-decoration:none;cursor:pointer;">Get Key</a>' +
            '</label>' +
            '<input id="sls-morr" type="text" value="%MORR_KEY%" style="width:100%;box-sizing:border-box;background:#0d0e12;border:1px solid #333640;color:#fff;padding:10px 12px;border-radius:6px;margin-bottom:15px;font-family:monospace;font-size:13px;outline:none;" placeholder="Optional..."/>' +
            '<label style="display:flex;justify-content:space-between;margin-bottom:8px;font-size:12px;font-weight:500;color:#b4b6bc;">' +
            '<span>Ryuu API Key</span>' +
            '<a href="https://generator.ryuu.lol/" target="_blank" style="color:#007bff;text-decoration:none;cursor:pointer;">Get Key</a>' +
            '</label>' +
            '<input id="sls-ryuu" type="text" value="%RYUU_KEY%" style="width:100%;box-sizing:border-box;background:#0d0e12;border:1px solid #333640;color:#fff;padding:10px 12px;border-radius:6px;margin-bottom:25px;font-family:monospace;font-size:13px;outline:none;" placeholder="Optional..."/>' +
            '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
                '<button id="sls-cancel" style="background:transparent;border:1px solid #333640;color:#e8e9eb;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;">Cancel</button>' +
                '<button id="sls-save" style="background:#007bff;border:none;color:#fff;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;box-shadow:0 4px 10px rgba(0,123,255,0.3);">Save</button>' +
            '</div>' +
        '</div>';
        document.body.appendChild(overlay);
        
        document.getElementById('sls-cancel').onclick = function() { overlay.remove(); };
        document.getElementById('sls-save').onclick = function() {
            var morr = document.getElementById('sls-morr').value;
            var ryuu = document.getElementById('sls-ryuu').value;
            window.location.hash = 'sls-auth-MORR=' + encodeURIComponent(morr) + '&RYUU=' + encodeURIComponent(ryuu) + '-TS=' + Date.now();
            overlay.remove();
            
            // Brief success toast
            var toast = document.createElement('div');
            toast.innerText = 'API Settings Saved!';
            toast.style.cssText = 'position:fixed;bottom:30px;right:30px;background:#28a745;color:#fff;padding:12px 20px;border-radius:8px;font-family:Inter,sans-serif;font-weight:500;z-index:999999;box-shadow:0 5px 15px rgba(0,0,0,0.3);transition:opacity 0.5s;';
            document.body.appendChild(toast);
            setTimeout(function(){ toast.style.opacity = '0'; }, 2000);
            setTimeout(function(){ toast.remove(); }, 2500);
        };
    }

    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function() {
            debounceTimer = null;
            addButtons();
        });
    }

    addButtons();
    observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
})();
)JSRAW";

        std::string morrKey = g_config.morrenusKey.get();
        std::string ryuuKey = g_config.ryuuKey.get();

        size_t pos;
        if ((pos = storePageScript.find("%MORR_KEY%")) != std::string::npos)
            storePageScript.replace(pos, 10, morrKey);
        if ((pos = storePageScript.find("%RYUU_KEY%")) != std::string::npos)
            storePageScript.replace(pos, 10, ryuuKey);
        for (auto& page : pages)
        {
            if (page.url.find("store.steampowered.com") != std::string::npos && !page.webSocketDebuggerUrl.empty())
            {
                injectJS(page.webSocketDebuggerUrl, storePageScript);
            }
        }
    }
}
