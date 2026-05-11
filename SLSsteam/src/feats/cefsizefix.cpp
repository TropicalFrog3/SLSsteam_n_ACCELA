#include "cefsizefix.hpp"

#include "../config.hpp"
#include "../log.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Marker comments used to identify injected content
static const char* INJECT_MARKER_BEGIN = "<!-- SLS_SIZE_FIX_BEGIN -->";
static const char* INJECT_MARKER_END   = "<!-- SLS_SIZE_FIX_END -->";

/**
 * Locate Steam's steamui/index.html.
 * Checks common Steam installation paths.
 */
static std::filesystem::path findSteamUIIndexHtml()
{
    const char* home = getenv("HOME");
    if (!home) return {};

    // Common Steam installation paths
    std::filesystem::path candidates[] = {
        std::filesystem::path(home) / ".local" / "share" / "Steam" / "steamui" / "index.html",
        std::filesystem::path(home) / ".steam" / "steam" / "steamui" / "index.html",
        std::filesystem::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam" / "steamui" / "index.html",
    };

    for (auto& path : candidates)
    {
        if (std::filesystem::exists(path))
        {
            return path;
        }
    }

    return {};
}

/**
 * Generate the inline JavaScript that monkey-patches
 * SteamClient.Apps.RegisterForAppDetails to inject correct
 * disk space values for SLS-unlocked games.
 */
static std::string generateSizeFixJS()
{
    return R"JS(
(function() {
    'use strict';
    if (window.__slsInjected) return;
    window.__slsInjected = true;

    var sizeCache = {};
    try {
        var stored = localStorage.getItem('sls_sizeCache');
        if (stored) sizeCache = JSON.parse(stored);
    } catch(e) {}
    var pendingFetches = {};

    function fetchAppSize(appId) {
        if (sizeCache[appId] !== undefined)
            return Promise.resolve(sizeCache[appId]);
        if (pendingFetches[appId])
            return new Promise(function(resolve) { pendingFetches[appId].push(resolve); });
        pendingFetches[appId] = [];

        return new Promise(function(resolve) {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', 'https://api.steamcmd.net/v1/info/' + appId, true);
            xhr.timeout = 8000;
            xhr.onload = function() {
                var totalSize = 0;
                try {
                    var data = JSON.parse(xhr.responseText);
                    var appData = data.data && data.data[appId];
                    if (appData && appData.depots) {
                        var commonSize = 0;
                        var linuxSize = 0;
                        var windowsSize = 0;
                        var macosSize = 0;
                        var otherTotal = 0;
                        var otherCount = 0;

                        for (var depotId in appData.depots) {
                            if (!appData.depots.hasOwnProperty(depotId)) continue;
                            var depot = appData.depots[depotId];
                            if (!depot.manifests || !depot.manifests.public) continue;
                            
                            var size = parseInt(depot.manifests.public.size || '0', 10);
                            if (isNaN(size)) continue;

                            var oslist = (depot.config && depot.config.oslist) ? depot.config.oslist : "";
                            if (!oslist) {
                                commonSize += size;
                            } else if (oslist.indexOf("linux") !== -1) {
                                linuxSize += size;
                            } else if (oslist.indexOf("windows") !== -1) {
                                windowsSize += size;
                            } else if (oslist.indexOf("macos") !== -1) {
                                macosSize += size;
                            } else {
                                otherTotal += size;
                                otherCount++;
                            }
                        }

                        if (linuxSize > 0) totalSize = commonSize + linuxSize;
                        else if (windowsSize > 0) totalSize = commonSize + windowsSize;
                        else if (macosSize > 0) totalSize = commonSize + macosSize;
                        else if (otherCount > 0) totalSize = commonSize + (otherTotal / otherCount);
                        else totalSize = commonSize;
                    }
                } catch (e) {
                    console.warn('[SLS] Failed to parse size for app ' + appId + ':', e);
                }
                sizeCache[appId] = totalSize;
                if (totalSize > 0) {
                    try { localStorage.setItem('sls_sizeCache', JSON.stringify(sizeCache)); } catch(e) {}
                }
                resolve(totalSize);
                if (pendingFetches[appId]) {
                    pendingFetches[appId].forEach(function(cb) { cb(totalSize); });
                    delete pendingFetches[appId];
                }
            };
            xhr.onerror = xhr.ontimeout = function() {
                console.warn('[SLS] Network error fetching size for app ' + appId);
                sizeCache[appId] = 0;
                resolve(0);
                if (pendingFetches[appId]) {
                    pendingFetches[appId].forEach(function(cb) { cb(0); });
                    delete pendingFetches[appId];
                }
            };
            xhr.send();
        });
    }

    function needsSizeInjection(details) {
        return details &&
            details.lDiskSpaceRequiredBytes === 0 &&
            details.nBuildID === 0 &&
            details.bIsSubscribedTo === true &&
            !details.bHasAnyLocalContent;
    }

    function applyFix(details) {
        if (sizeCache[details.unAppID] > 0) {
            details.lDiskSpaceRequiredBytes = sizeCache[details.unAppID];
            return;
        }
        fetchAppSize(details.unAppID).then(function(size) {
            if (size > 0 && typeof appDetailsStore !== 'undefined') {
                var appData = appDetailsStore.GetAppData(details.unAppID);
                if (appData && appData.details) {
                    appData.details.lDiskSpaceRequiredBytes = size;
                    if (appData.listeners) {
                        appData.listeners.forEach(function(listener) {
                            try { listener(appData.details); } catch(e) {}
                        });
                    }
                }
            }
        });
    }

    function waitForSteamClient(callback) {
        if (typeof SteamClient !== 'undefined' && SteamClient.Apps &&
            typeof SteamClient.Apps.RegisterForAppDetails === 'function') {
            callback();
            return;
        }
        var attempts = 0;
        var interval = setInterval(function() {
            attempts++;
            if (typeof SteamClient !== 'undefined' && SteamClient.Apps &&
                typeof SteamClient.Apps.RegisterForAppDetails === 'function') {
                clearInterval(interval);
                callback();
            } else if (attempts > 100) {
                clearInterval(interval);
            }
        }, 100);
    }

    waitForSteamClient(function() {
        var originalRegister = SteamClient.Apps.RegisterForAppDetails;
        SteamClient.Apps.RegisterForAppDetails = function(appId, callback) {
            var wrappedCallback = function(details) {
                if (needsSizeInjection(details)) {
                    applyFix(details);
                }
                return callback(details);
            };
            return originalRegister.call(this, appId, wrappedCallback);
        };

        // Fix already-registered apps
        if (typeof appDetailsStore !== 'undefined' && appDetailsStore.m_mapAppData) {
            appDetailsStore.m_mapAppData.forEach(function(data, appId) {
                if (data.details && needsSizeInjection(data.details)) {
                    applyFix(data.details);
                }
            });
        }

        console.log('[SLS] Install size fix active');
    });
})();
)JS";
}

bool CefSizeFix::injectSizeFixScript()
{
    auto indexPath = findSteamUIIndexHtml();
    if (indexPath.empty())
    {
        g_pLog->warn("CefSizeFix: Could not find steamui/index.html\n");
        return false;
    }

    g_pLog->info("CefSizeFix: Found steamui/index.html at %s\n", indexPath.string().c_str());

    // Read the current content
    std::ifstream inFile(indexPath);
    if (!inFile.is_open())
    {
        g_pLog->warn("CefSizeFix: Could not open %s for reading\n", indexPath.string().c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    std::string content = buffer.str();
    inFile.close();

    // Check if already injected
    if (content.find(INJECT_MARKER_BEGIN) != std::string::npos)
    {
        g_pLog->info("CefSizeFix: Script already injected, skipping\n");
        return true;
    }

    // Build the injection: an inline <script> tag placed before </head>
    std::string injection = std::string(INJECT_MARKER_BEGIN) + "\n"
        + "<script>\n"
        + generateSizeFixJS()
        + "\n</script>\n"
        + INJECT_MARKER_END + "\n";

    // Find the </head> tag and inject before it
    auto headEnd = content.find("</head>");
    if (headEnd == std::string::npos)
    {
        g_pLog->warn("CefSizeFix: Could not find </head> in index.html\n");
        return false;
    }

    content.insert(headEnd, injection);

    // Write back
    std::ofstream outFile(indexPath, std::ios::trunc);
    if (!outFile.is_open())
    {
        g_pLog->warn("CefSizeFix: Could not open %s for writing\n", indexPath.string().c_str());
        return false;
    }

    outFile << content;
    outFile.close();

    g_pLog->info("CefSizeFix: Successfully injected size fix script into index.html\n");
    return true;
}

void CefSizeFix::removeSizeFixScript()
{
    auto indexPath = findSteamUIIndexHtml();
    if (indexPath.empty())
    {
        return;
    }

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

    // Remove everything from beginPos to the end of the marker + newline
    auto removeEnd = endPos + strlen(INJECT_MARKER_END);
    if (removeEnd < content.size() && content[removeEnd] == '\n')
        removeEnd++;

    content.erase(beginPos, removeEnd - beginPos);

    std::ofstream outFile(indexPath, std::ios::trunc);
    if (!outFile.is_open()) return;

    outFile << content;
    outFile.close();

    g_pLog->info("CefSizeFix: Removed injected script from index.html\n");
}
