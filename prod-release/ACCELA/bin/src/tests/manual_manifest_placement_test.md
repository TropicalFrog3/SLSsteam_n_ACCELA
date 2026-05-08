# Manual Manifest Placement Test — SLSsteam/ACCELA Integration

**Date:** 2026-04-22  
**Requirement:** Req 8 — Manifest Storage and Manual Placement  
**Type:** Manual Test

## Objective

Verify that `.manifest` files placed manually into `$SteamRoot/config/depotcache/` at any time are automatically discovered and included by `ManifestPackager.package()` when a download is triggered, without requiring any application state, file watching, caching, or directory enumeration.

---

## Prerequisites

1. A working Steam installation on Linux
2. ACCELA installed and functional
3. At least one Lua plugin file in `$SteamRoot/config/stplug-in/` (e.g., `293780.lua`)
4. Python 3 with access to ACCELA's `manifest_packager.py`

---

## Test Procedure

### Step 1: Identify Test AppID and Depot

1. Choose an existing Lua plugin file from `$SteamRoot/config/stplug-in/` (e.g., `293780.lua`)
2. Read the file and identify a depot ID and manifest GID from a `setManifestid()` call:
   ```lua
   setManifestid(293781, "9207527406397102173")
   ```
3. Note the expected manifest filename: `{depotId}_{manifestGid}.manifest`  
   Example: `293781_9207527406397102173.manifest`

### Step 2: Verify Baseline (No Manifest)

1. Ensure the manifest file does NOT exist in `$SteamRoot/config/depotcache/`:
   ```bash
   ls -la ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest
   # Should return: No such file or directory
   ```

2. Run `manifest_packager.py` for the AppID:
   ```bash
   python3 ACCELA-20260127111800-linux-source/bin/src/scripts/manifest_packager.py 293780
   ```

3. **Expected result:**
   - WARNING log: `Manifest not found for depot 293781 at .../depotcache/293781_9207527406397102173.manifest, skipping`
   - ZIP is still created (contains only the Lua file)
   - Exit code: 0

4. Verify the ZIP contents:
   ```bash
   unzip -l /tmp/accela_pkg_*/293780.zip
   # Should show: 293780.lua only (no .manifest files)
   ```

### Step 3: Manually Place Manifest File

1. Create a fake manifest file (or copy a real one if available):
   ```bash
   echo "FAKE_MANIFEST_CONTENT_FOR_TESTING" > ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest
   ```

2. Verify the file exists:
   ```bash
   ls -la ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest
   # Should show the file with size > 0
   ```

### Step 4: Trigger ManifestPackager Again

1. Run `manifest_packager.py` again for the same AppID:
   ```bash
   python3 ACCELA-20260127111800-linux-source/bin/src/scripts/manifest_packager.py 293780
   ```

2. **Expected result:**
   - NO WARNING about missing manifest
   - INFO log: `Packaged AppID 293780: /tmp/accela_pkg_xyz/293780.zip (lua + 1 manifest(s))`
   - Exit code: 0

3. Verify the ZIP contents:
   ```bash
   ZIP_PATH=$(python3 ACCELA-20260127111800-linux-source/bin/src/scripts/manifest_packager.py 293780 2>/dev/null)
   unzip -l "$ZIP_PATH"
   # Should show:
   #   293780.lua
   #   293781_9207527406397102173.manifest
   ```

4. Extract and verify the manifest content:
   ```bash
   unzip -p "$ZIP_PATH" 293781_9207527406397102173.manifest
   # Should output: FAKE_MANIFEST_CONTENT_FOR_TESTING
   ```

### Step 5: Verify No Caching or State Dependency

1. Delete the manually placed manifest:
   ```bash
   rm ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest
   ```

2. Run `manifest_packager.py` again:
   ```bash
   python3 ACCELA-20260127111800-linux-source/bin/src/scripts/manifest_packager.py 293780
   ```

3. **Expected result:**
   - WARNING log returns: `Manifest not found for depot 293781 at ...`
   - ZIP contains only the Lua file again
   - This proves no caching — each invocation performs a fresh direct path lookup

### Step 6: Verify Timing Independence

1. Place the manifest file again:
   ```bash
   echo "MANIFEST_PLACED_AFTER_ACCELA_CLOSED" > ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest
   ```

2. **Without starting ACCELA or any other application**, run `manifest_packager.py`:
   ```bash
   python3 ACCELA-20260127111800-linux-source/bin/src/scripts/manifest_packager.py 293780
   ```

3. **Expected result:**
   - Manifest is discovered and included in the ZIP
   - This proves the packager does not require ACCELA or SLSsteam to be running when the manifest is placed

---

## Acceptance Criteria

| Criterion | Status | Notes |
|-----------|--------|-------|
| Manually placed manifests are discovered via direct path lookup | ✅ / ❌ | Step 4 |
| No file watching, caching, or directory enumeration is used | ✅ / ❌ | Step 5 |
| Manifests can be placed before, during, or after ACCELA sessions | ✅ / ❌ | Step 6 |
| Missing manifests produce WARNING logs, not errors | ✅ / ❌ | Step 2 |
| ZIP assembly succeeds even when manifests are missing | ✅ / ❌ | Step 2 |

---

## Cleanup

```bash
# Remove test manifest file
rm ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest

# Remove temporary ZIP files
rm -rf /tmp/accela_pkg_*
```

---

## Notes

- This test validates **Req 8 AC3–AC5**: direct path lookup, no caching, no state dependency
- The test uses a fake manifest file because real Steam manifests are binary and depot-specific
- The test can be repeated with multiple depot IDs from the same Lua plugin to verify batch discovery

---

## Test Result

**Date Executed:** _____________  
**Tester:** _____________  
**Result:** ✅ PASS / ❌ FAIL  
**Notes:**

