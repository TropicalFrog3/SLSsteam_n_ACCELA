#include "cdpinject.hpp"
#include "../log.hpp"
#include "../config.hpp"

#include <cstring>
#include <random>
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
                    var span = luaLink.querySelector('span');
                    if (span) span.innerText = 'Download Lua';
                    luaLink.style.filter = 'hue-rotate(110deg) brightness(1.2)';
                    luaLink.onclick = function(e) {
                        e.preventDefault();
                        e.stopPropagation();
                        // Immediate visual feedback
                        var clickSpan = luaLink.querySelector('span');
                        if (clickSpan) clickSpan.innerText = '⏳ Downloading...';
                        luaLink.style.filter = 'hue-rotate(50deg) brightness(1.0)';
                        luaLink.style.pointerEvents = 'none';
                        luaLink.style.opacity = '0.8';
                        // Store appid on the button for status updates from C++
                        luaBtn.dataset.slsAppid = productID;
                        ping('Lua Click: ' + productID);
                        window.location.hash = 'sls-click-' + productID + '-' + Date.now();
                    };
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
                    if (spanSet) spanSet.innerText = '⚙️';
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
            '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">⚙️ API Settings</h2>' +
            '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Configure your credentials for 3rd-party download APIs.</p>' +
            '<label style="display:flex;justify-content:space-between;margin-bottom:8px;font-size:12px;font-weight:500;color:#b4b6bc;">' +
            '<span>Morrenus API Key</span>' +
            '<a href="https://manifest.morrenus.xyz/api-keys/stats" target="_blank" style="color:#007bff;text-decoration:none;cursor:pointer;">Get Key \u2197</a>' +
            '</label>' +
            '<input id="sls-morr" type="text" value="%MORR_KEY%" style="width:100%;box-sizing:border-box;background:#0d0e12;border:1px solid #333640;color:#fff;padding:10px 12px;border-radius:6px;margin-bottom:15px;font-family:monospace;font-size:13px;outline:none;" placeholder="Optional..."/>' +
            '<label style="display:flex;justify-content:space-between;margin-bottom:8px;font-size:12px;font-weight:500;color:#b4b6bc;">' +
            '<span>Ryuu API Key</span>' +
            '<a href="https://generator.ryuu.lol/" target="_blank" style="color:#007bff;text-decoration:none;cursor:pointer;">Get Key \u2197</a>' +
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
            toast.innerText = '✅ API Settings Saved!';
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
            if (page.webSocketDebuggerUrl.empty()) continue;

            // Filter for Steam store pages: /app/ or /sub/
            if (page.url.find("steampowered.com/app/") != std::string::npos ||
                page.url.find("steampowered.com/sub/") != std::string::npos)
            {
                g_pLog->debug("CDPInject: Injecting into: %s (%s)\n", page.title.c_str(), page.url.c_str());
                injectJS(page.webSocketDebuggerUrl, storePageScript);
            }
        }
    }
}
