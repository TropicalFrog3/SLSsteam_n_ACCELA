# Error Path Logging Audit — SLSsteam/ACCELA Integration

**Date:** 2026-04-22  
**Requirement:** Req 11 — Error Handling and Logging

## Summary

All error paths produce structured, actionable log messages as specified in Requirement 11. This audit verifies each component's logging against the design specification.

---

## Manifest_Packager (`scripts/manifest_packager.py`)

| Error Condition | Expected Log | Status | Line(s) |
|----------------|--------------|--------|---------|
| SteamRoot not found | ERROR with searched paths | ✅ PASS | L92 |
| Lua file missing | ERROR with AppID and path | ✅ PASS | L107 |
| Lua file malformed (no `addappid`) | ERROR with AppID and reason | ✅ PASS | L128 |
| Lua file read error | ERROR with AppID, path, and exception | ✅ PASS | L118 |
| Manifest file missing | WARNING with depot ID and path | ✅ PASS | L155-157 |
| ZIP assembly failure | ERROR with AppID, reason, cleaned-up path | ✅ PASS | L187 |
| Success | INFO with AppID, ZIP path, manifest count | ✅ PASS | L190-192 |

**Sample log messages:**
```
[manifest_packager] ERROR: Cannot locate Steam root: searched [...]
[manifest_packager] ERROR: Lua plugin not found for AppID 293780 at /path/to/293780.lua
[manifest_packager] ERROR: Malformed Lua plugin for AppID 293780: no addappid entries found in /path/to/293780.lua
[manifest_packager] WARNING: Manifest not found for depot 293781 at /path/to/depotcache/293781_9207527406397102173.manifest, skipping
[manifest_packager] ERROR: ZIP assembly failed for AppID 293780: [Errno 28] No space left on device. Cleaned up /tmp/accela_pkg_xyz/293780.zip
[manifest_packager] INFO: Packaged AppID 293780: /tmp/accela_pkg_xyz/293780.zip (lua + 2 manifest(s))
```

---

## Download_Hook (`SLSsteam/src/hooks.cpp`)

| Error Condition | Expected Log | Status | Line(s) |
|----------------|--------------|--------|---------|
| Helper script launch (intercept) | INFO with AppID | ✅ PASS | L359 |
| fork() failure | WARNING with AppID | ✅ PASS | L373 |

**Sample log messages:**
```
Download_Hook: intercepted AppID 293780
Download_Hook: fork() failed for AppID 293780
```

---

## Lua Scanner (`SLSsteam/src/config.cpp`)

| Error Condition | Expected Log | Status | Line(s) |
|----------------|--------------|--------|---------|
| HOME env var not set | WARNING | ✅ PASS | L499 |
| SteamRoot not found | WARNING with searched paths | ✅ PASS | L520 |
| Plugin directory missing | INFO (not an error) | ✅ PASS | L528 |
| Cannot open Lua file | WARNING with path | ✅ PASS | L551 |
| Malformed Lua file (no `addappid`) | WARNING with path | ✅ PASS | L563 |
| AppID found | DEBUG with AppID and path | ✅ PASS | L571 |
| AppID already in config | DEBUG (duplicate skip) | ✅ PASS | L407 |
| AppID added to config | INFO with AppID | ✅ PASS | L488, L582 |
| Scan complete | INFO with count | ✅ PASS | L589 |

**Sample log messages:**
```
scanLuaPluginsAndUpdateConfig: HOME env var not set, cannot resolve SteamRoot
scanLuaPluginsAndUpdateConfig: Cannot locate SteamRoot (searched /home/user/.steam/steam and /home/user/.local/share/Steam)
scanLuaPluginsAndUpdateConfig: Plugin directory does not exist: /home/user/.steam/steam/config/stplug-in
scanLuaPluginsAndUpdateConfig: Cannot open Lua plugin: /path/to/293780.lua
Malformed Lua plugin: /path/to/293780.lua
scanLuaPluginsAndUpdateConfig: Found AppID 293780 in /path/to/293780.lua
scanLuaPluginsAndUpdateConfig: Adding AppID 293780 to AdditionalApps
addAdditionalAppId: Appended AppID 293780 to AdditionalApps in /home/user/.config/SLSsteam/config.yaml
scanLuaPluginsAndUpdateConfig: Scan complete, processed 5 Lua plugin(s)
```

---

## accela-download.sh Helper Script

| Error Condition | Expected Log | Status | Line(s) |
|----------------|--------------|--------|---------|
| Packaging failed | stderr message with AppID | ✅ PASS | L11-13 |

**Sample log message:**
```
[accela-download] Packaging failed for AppID 293780
```

---

## CLI_Handler (Post-Processing)

The CLI_Handler's post-processing pipeline already has comprehensive logging for:
- Manifest placement (via `_move_manifests_to_depotcache`)
- Config sync (via `_add_appids_to_slssteam_config`)
- Pipeline completion

These were not modified in this integration (existing behavior preserved), so they inherit the existing logging infrastructure.

---

## Verification Commands

```bash
# Manifest_Packager error paths
python3 scripts/manifest_packager.py 999999  # Non-existent AppID
python3 scripts/manifest_packager.py invalid  # Invalid AppID format

# Lua Scanner (requires SLSsteam build)
# Logs appear in Steam's console when SLSsteam.so is injected

# Download_Hook (requires SLSsteam build + Steam client)
# Logs appear when clicking download on an unlocked, non-owned app
```

---

## Conclusion

✅ **PASS** — All error paths produce the documented log messages at the correct log levels (ERROR, WARNING, INFO, DEBUG) as specified in Requirement 11.

**Log Level Summary:**
- **ERROR**: Unrecoverable failures (missing Lua, SteamRoot not found, ZIP assembly failure)
- **WARNING**: Recoverable issues (missing manifests, malformed Lua files, fork failure)
- **INFO**: Normal operation milestones (intercept, packaging success, AppID added)
- **DEBUG**: Detailed trace information (AppID found, duplicate skip)

All components follow structured logging patterns with sufficient context (AppIDs, paths, reasons) for debugging.
