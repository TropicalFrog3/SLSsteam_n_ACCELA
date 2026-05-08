# End-to-End Integration Test — SLSsteam/ACCELA Integration

**Date:** 2026-04-22  
**Requirements:** Req 5, 6, 9 — Full Pipeline Integration  
**Type:** Integration Test

## Objective

Verify the complete integration flow from Download_Hook → Helper Script → ManifestPackager → ACCELA CLI → Download → Config Sync, ensuring all components work together correctly.

---

## Prerequisites

1. **Steam Client** running on Linux with SLSsteam.so injected
2. **SLSsteam** built and configured with:
   - `config.yaml` at `~/.config/SLSsteam/config.yaml`
   - At least one unlocked AppID in `AdditionalApps`
   - Corresponding Lua plugin file in `$SteamRoot/config/stplug-in/`
3. **ACCELA** installed and functional
4. **accela-download** helper script installed to `$PATH` or configured in SLSsteam
5. Test AppID that is:
   - Unlocked in SLSsteam (`AdditionalApps`)
   - NOT legitimately owned (`!isSubscribed`)
   - Has a valid Lua plugin file with depot keys and manifest GIDs

---

## Test Flow

```
User clicks Download in Steam
    ↓
Download_Hook (SLSsteam C++)
    ↓
accela-download.sh (fork+exec)
    ↓
manifest_packager.py (produces ZIP)
    ↓
ACCELA CLI (ProcessZipTask → DownloadDepotsTask)
    ↓
Post-Processing (ACF, manifests, config sync)
    ↓
Verification (config.yaml updated, manifests placed)
```

---

## Test Procedure

### Phase 1: Hook → Helper Script

**Objective:** Verify Download_Hook intercepts the download action and launches the helper script.

1. **Setup:**
   - Identify a test AppID (e.g., 293780) that is unlocked but not owned
   - Ensure the Lua plugin exists: `~/.steam/steam/config/stplug-in/293780.lua`
   - Ensure SLSsteam is injected into Steam

2. **Action:**
   - Open Steam Library
   - Find the unlocked game (AppID 293780)
   - Click the "Download" button

3. **Expected Result:**
   - Steam console log shows: `Download_Hook: intercepted AppID 293780`
   - Helper script `accela-download` is launched (check with `ps aux | grep accela-download`)
   - Original Steam download is suppressed (no Steam download progress bar)

4. **Verification:**
   ```bash
   # Check SLSsteam log for intercept message
   grep "Download_Hook: intercepted" ~/.steam/steam/logs/console_log.txt
   ```

**Status:** ✅ / ❌

---

### Phase 2: Helper Script → ManifestPackager

**Objective:** Verify `accela-download.sh` runs `manifest_packager.py` and produces a ZIP.

1. **Setup:**
   - Monitor `/tmp/accela_pkg_*` for new directories
   - Tail the helper script output (if logging to a file)

2. **Action:**
   - (Triggered by Phase 1 — Download_Hook launches the helper)

3. **Expected Result:**
   - `manifest_packager.py` is invoked with the AppID
   - A ZIP file is created in `/tmp/accela_pkg_*/293780.zip`
   - Helper script logs: `[manifest_packager] INFO: Packaged AppID 293780: /tmp/accela_pkg_xyz/293780.zip (lua + N manifest(s))`

4. **Verification:**
   ```bash
   # Check for ZIP creation
   ls -la /tmp/accela_pkg_*/293780.zip
   
   # Verify ZIP contents
   unzip -l /tmp/accela_pkg_*/293780.zip
   # Should show: 293780.lua + *.manifest files
   ```

**Status:** ✅ / ❌

---

### Phase 3: Helper Script → ACCELA CLI

**Objective:** Verify the helper script launches ACCELA CLI with the pre-built ZIP.

1. **Setup:**
   - Monitor ACCELA process: `ps aux | grep ACCELA`
   - Check ACCELA logs (if available)

2. **Action:**
   - (Triggered by Phase 2 — helper script calls `run.sh -cli <zip_path>`)

3. **Expected Result:**
   - ACCELA CLI starts in a new terminal window
   - `ProcessZipTask.run()` is invoked with the ZIP path
   - ACCELA parses the ZIP and extracts `game_data` (appid, depots, dlcs, app_token, manifests)

4. **Verification:**
   ```bash
   # Check ACCELA process
   ps aux | grep "run.sh -cli"
   
   # Check ACCELA logs for ZIP processing
   # (Log location depends on ACCELA configuration)
   ```

**Status:** ✅ / ❌

---

### Phase 4: ACCELA CLI → Download

**Objective:** Verify ACCELA downloads depot content via DepotDownloader.

1. **Setup:**
   - Monitor download progress in ACCELA UI/CLI
   - Check destination directory (e.g., `~/.steam/steam/steamapps/common/<game>/`)

2. **Action:**
   - (Triggered by Phase 3 — ACCELA runs `DownloadDepotsTask`)

3. **Expected Result:**
   - DepotDownloader is invoked for each depot
   - Game files are downloaded to the destination directory
   - Download completes without errors

4. **Verification:**
   ```bash
   # Check destination directory for game files
   ls -la ~/.steam/steam/steamapps/common/<game>/
   
   # Verify depot content exists
   du -sh ~/.steam/steam/steamapps/common/<game>/
   ```

**Status:** ✅ / ❌

---

### Phase 5: Post-Processing → Manifest Placement

**Objective:** Verify manifests are moved to `$SteamRoot/config/depotcache/`.

1. **Setup:**
   - Note the depot IDs and manifest GIDs from the Lua plugin
   - Check `$SteamRoot/config/depotcache/` before and after

2. **Action:**
   - (Triggered by Phase 4 — ACCELA post-processing runs `_move_manifests_to_depotcache`)

3. **Expected Result:**
   - Manifest files are placed at `$SteamRoot/config/depotcache/{depotId}_{manifestGid}.manifest`
   - Naming convention is correct: `{depotId}_{manifestGid}.manifest`
   - No manifests are left in the download destination directory

4. **Verification:**
   ```bash
   # Check depotcache for new manifests
   ls -la ~/.steam/steam/config/depotcache/ | grep 293781
   
   # Verify naming convention
   # Example: 293781_9207527406397102173.manifest
   ```

**Status:** ✅ / ❌

---

### Phase 6: Post-Processing → Config Sync

**Objective:** Verify AppID, token, and DLC data are written to `config.yaml`.

1. **Setup:**
   - Backup `~/.config/SLSsteam/config.yaml` before the test
   - Note the current `AdditionalApps` and `AppTokens` entries

2. **Action:**
   - (Triggered by Phase 4 — ACCELA post-processing runs `_add_appids_to_slssteam_config`)

3. **Expected Result:**
   - AppID is added to `AdditionalApps` (if not already present)
   - App token is added to `AppTokens`
   - DLC data is added to `DlcData` (if >64 DLCs)
   - Config file is written atomically (no corruption)

4. **Verification:**
   ```bash
   # Check AdditionalApps
   grep -A 5 "AdditionalApps:" ~/.config/SLSsteam/config.yaml | grep 293780
   
   # Check AppTokens
   grep -A 5 "AppTokens:" ~/.config/SLSsteam/config.yaml | grep 293780
   
   # Verify token value matches Lua plugin
   grep "addtoken" ~/.steam/steam/config/stplug-in/293780.lua
   ```

**Status:** ✅ / ❌

---

## Acceptance Criteria

| Criterion | Status | Phase |
|-----------|--------|-------|
| Full pipeline completes without error | ✅ / ❌ | All |
| `config.yaml` contains the new AppID in `AdditionalApps` | ✅ / ❌ | Phase 6 |
| `config.yaml` contains the app token in `AppTokens` | ✅ / ❌ | Phase 6 |
| Manifests end up in `$SteamRoot/config/depotcache/` | ✅ / ❌ | Phase 5 |
| Game files are downloaded to the correct location | ✅ / ❌ | Phase 4 |
| Download_Hook does not block Steam UI | ✅ / ❌ | Phase 1 |

---

## Error Scenarios

### Scenario 1: Lua Plugin Missing

1. Remove the Lua plugin: `rm ~/.steam/steam/config/stplug-in/293780.lua`
2. Click Download in Steam
3. **Expected:** `manifest_packager.py` logs ERROR and exits 1; ACCELA does not start

### Scenario 2: Manifest Files Missing

1. Ensure no manifests exist in depotcache for the test AppID
2. Click Download in Steam
3. **Expected:** `manifest_packager.py` logs WARNING for each missing manifest; ZIP contains only Lua file; download proceeds with available depots

### Scenario 3: fork() Failure (Simulated)

1. (Requires modifying SLSsteam to simulate fork() failure)
2. Click Download in Steam
3. **Expected:** `Download_Hook` logs WARNING; original Steam download proceeds

---

## Cleanup

```bash
# Remove test game files
rm -rf ~/.steam/steam/steamapps/common/<game>/

# Remove test manifests
rm ~/.steam/steam/config/depotcache/293781_*.manifest

# Restore config.yaml backup
cp ~/.config/SLSsteam/config.yaml.backup ~/.config/SLSsteam/config.yaml

# Remove temporary ZIP files
rm -rf /tmp/accela_pkg_*
```

---

## Test Result

**Date Executed:** _____________  
**Tester:** _____________  
**Result:** ✅ PASS / ❌ FAIL  
**Notes:**

