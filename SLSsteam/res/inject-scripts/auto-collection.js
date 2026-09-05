/**
 * Auto-Collection Script for SLSsteam
 * Injected into SteamUI to automatically add all spoofed games to a collection named "LUAs".
 */
(function() {
    console.log("[AutoCollection] Script injected. Waiting for collectionStore...");
    
    // Poll until collectionStore is ready
    let maxRetries = 100;
    let pollInterval = setInterval(() => {
        if (window.collectionStore && window.collectionStore.GetUserCollectionsByName) {
            clearInterval(pollInterval);
            console.log("[AutoCollection] collectionStore is ready. Fetching app list...");
            
            // Fetch list of spoofed apps from our local backend
            fetch("http://127.0.0.1:9001/list")
                .then(r => r.json())
                .then(apps => {
                    console.log(`[AutoCollection] Fetched ${apps.length} spoofed apps.`);
                    if (apps.length === 0) return;
                    
                    // Look for the "LUAs" collection
                    let collections = window.collectionStore.GetUserCollectionsByName("LUAs");
                    if (!collections || collections.length === 0) {
                        console.log("[AutoCollection] LUAs collection not found, creating it automatically...");
                        try {
                            let newCol = window.collectionStore.NewUnsavedCollection("LUAs", null, []);
                            window.collectionStore.userCollections.push(newCol);
                            if (window.collectionStore.m_mapCollectionsFromStorage) {
                                window.collectionStore.m_mapCollectionsFromStorage.set(newCol.id, newCol);
                            }
                            window.collectionStore.SaveCollection(newCol);
                            collections = [newCol];
                            console.log(`[AutoCollection] Successfully created LUAs collection (ID: ${newCol.id}).`);
                        } catch (e) {
                            console.error("[AutoCollection] Failed to automatically create LUAs collection:", e);
                            return;
                        }
                    }
                    
                    let colId = collections[0].id;
                    console.log(`[AutoCollection] Found LUAs collection (ID: ${colId}). Adding apps...`);
                    
                    // Filter out apps that don't have a valid AppOverview (Steam UI will crash if we pass nulls)
                    let validApps = apps.filter(appId => {
                        if (!window.appStore || !window.appStore.GetAppOverviewByAppID) return true; // Fallback if API changed
                        let overview = window.appStore.GetAppOverviewByAppID(appId);
                        return overview !== null && overview !== undefined;
                    });
                    
                    console.log(`[AutoCollection] Filtered down to ${validApps.length} valid apps out of ${apps.length}.`);
                    if (validApps.length > 0) {
                        window.collectionStore.AddOrRemoveApp(validApps, true, colId);
                        console.log("[AutoCollection] Finished adding apps.");
                    }
                })
                .catch(e => {
                    console.error("[AutoCollection] Failed to fetch app list from backend: ", e);
                });
        }
        maxRetries--;
        if (maxRetries <= 0) {
            clearInterval(pollInterval);
            console.error("[AutoCollection] Timed out waiting for collectionStore.");
        }
    }, 500); // Check every 500ms
})();
