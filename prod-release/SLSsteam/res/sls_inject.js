/**
 * SLSsteam CEF Injection Script
 * 
 * This script monkey-patches SteamClient.Apps.RegisterForAppDetails
 * to inject correct disk space information for SLS-unlocked games
 * that would otherwise show "0 B" in the install dialog.
 *
 * The C++ backend returns lDiskSpaceRequiredBytes=0 for unlocked games
 * because it has no depot metadata. This script intercepts the callback
 * and fetches the real size from the Steam API.
 */
(function() {
    'use strict';

    // Guard against double-injection
    if (window.__slsInjected) return;
    window.__slsInjected = true;

    // Size cache: appId -> size in bytes
    var sizeCache = {};
    // Pending fetches: appId -> [callbacks]
    var pendingFetches = {};

    /**
     * Fetch the real install size for an app from the Steam CDN metadata.
     * Uses the steamcmd.net API as a reliable public source for depot sizes.
     */
    function fetchAppSize(appId) {
        if (sizeCache[appId] !== undefined) {
            return Promise.resolve(sizeCache[appId]);
        }
        if (pendingFetches[appId]) {
            return new Promise(function(resolve) {
                pendingFetches[appId].push(resolve);
            });
        }
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
                        var depots = appData.depots;
                        for (var depotId in depots) {
                            if (depots.hasOwnProperty(depotId)) {
                                var depot = depots[depotId];
                                if (depot && depot.manifests && depot.manifests.public) {
                                    var size = parseInt(depot.manifests.public.size || '0', 10);
                                    if (!isNaN(size)) totalSize += size;
                                }
                            }
                        }
                    }
                } catch (e) {
                    console.warn('[SLS] Failed to parse size for app ' + appId + ':', e);
                }
                sizeCache[appId] = totalSize;
                resolve(totalSize);
                // Resolve pending callbacks
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

    /**
     * Read the SLS config to get the list of unlocked app IDs.
     * Falls back to detecting apps with lDiskSpaceRequiredBytes==0 at runtime.
     */
    function getUnlockedAppIds() {
        // The SLS config is at ~/.local/share/SLSsteam/path/config.json
        // But we can't read files from JS in CEF. Instead, we rely on
        // runtime detection: if an app is "subscribed" but has 0 disk space
        // and 0 build ID, it's likely SLS-unlocked.
        return null; // Will use runtime detection
    }

    /**
     * Check if an app details object looks like an SLS-unlocked game
     * that needs size injection.
     */
    function needsSizeInjection(details) {
        return details &&
            details.lDiskSpaceRequiredBytes === 0 &&
            details.nBuildID === 0 &&
            details.bIsSubscribedTo === true &&
            !details.bHasAnyLocalContent;
    }

    // Wait for SteamClient to be available
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
                console.warn('[SLS] Timed out waiting for SteamClient');
            }
        }, 100);
    }

    waitForSteamClient(function() {
        console.log('[SLS] Patching SteamClient.Apps.RegisterForAppDetails');

        var originalRegister = SteamClient.Apps.RegisterForAppDetails;

        SteamClient.Apps.RegisterForAppDetails = function(appId, callback) {
            var wrappedCallback = function(details) {
                if (needsSizeInjection(details)) {
                    // Async fetch size, but apply immediately if cached
                    if (sizeCache[details.unAppID] > 0) {
                        details.lDiskSpaceRequiredBytes = sizeCache[details.unAppID];
                        console.log('[SLS] Injected cached size for app ' + details.unAppID + ': ' + details.lDiskSpaceRequiredBytes);
                    } else {
                        // Start fetch, will apply on next callback
                        fetchAppSize(details.unAppID).then(function(size) {
                            if (size > 0 && typeof appDetailsStore !== 'undefined') {
                                // Update the cached details object directly
                                var appData = appDetailsStore.GetAppData(details.unAppID);
                                if (appData && appData.details) {
                                    appData.details.lDiskSpaceRequiredBytes = size;
                                    console.log('[SLS] Async injected size for app ' + details.unAppID + ': ' + size);
                                    // Notify listeners to refresh UI
                                    if (appData.listeners) {
                                        appData.listeners.forEach(function(listener) {
                                            try { listener(appData.details); } catch(e) {}
                                        });
                                    }
                                }
                            }
                        });
                    }
                }
                return callback(details);
            };
            return originalRegister.call(this, appId, wrappedCallback);
        };

        // Also patch any already-registered apps
        if (typeof appDetailsStore !== 'undefined') {
            var mapAppData = appDetailsStore.m_mapAppData;
            if (mapAppData) {
                mapAppData.forEach(function(data, appId) {
                    if (data.details && needsSizeInjection(data.details)) {
                        fetchAppSize(appId).then(function(size) {
                            if (size > 0 && data.details) {
                                data.details.lDiskSpaceRequiredBytes = size;
                                console.log('[SLS] Patched existing details for app ' + appId + ': ' + size);
                                if (data.listeners) {
                                    data.listeners.forEach(function(listener) {
                                        try { listener(data.details); } catch(e) {}
                                    });
                                }
                            }
                        });
                    }
                });
            }
        }

        console.log('[SLS] SteamClient.Apps.RegisterForAppDetails patched successfully');
    });
})();
