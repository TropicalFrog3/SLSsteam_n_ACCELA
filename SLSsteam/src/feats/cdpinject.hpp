#pragma once

#include <string>
#include <vector>

namespace CDPInject
{
    /**
     * Represents a CEF/Chromium debug page from the /json endpoint.
     */
    struct CDPPage
    {
        std::string url;
        std::string title;
        std::string webSocketDebuggerUrl;
    };

    /**
     * Fetch the list of debuggable pages from CEF's /json endpoint.
     * Returns an empty vector on failure.
     */
    std::vector<CDPPage> fetchPages(const char* host = "127.0.0.1", int port = 8080);

    /**
     * Inject JavaScript into a page via its CDP WebSocket debugger URL.
     * Uses Runtime.evaluate over a minimal WebSocket client (RFC 6455).
     *
     * Returns true if the JS was sent successfully, false on any error.
     */
    bool injectJS(const std::string& wsUrl, const std::string& jsCode);

    /**
     * Run the full injection pass: fetch pages, filter for Steam store
     * pages, and inject the "Download Lua" button script into each.
     *
     * This is the direct C++ replacement for inject_cef.py.
     */
    void injectStorePages();

    /**
     * Use the Steam browser to download a URL by injecting fetch() into an existing page.
     * Bypasses Cloudflare and similar protections because it runs in a real browser context.
     * Writes the response body as raw bytes to destPath.
     *
     * Returns the HTTP status code on success, or -1 on failure.
     */
    int downloadViaPage(const std::string& url, const std::string& destPath);
}
