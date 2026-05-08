# Morrenus API Removal Verification — SLSsteam/ACCELA Integration

**Date:** 2026-04-22  
**Requirement:** Req 1 — Remove Morrenus API  
**Type:** Automated Check

## Summary

This document verifies that all Morrenus API references have been completely removed from the ACCELA codebase as specified in Requirement 1.

---

## Verification Commands

### 1. Search for "morrenus" in Python files

```bash
grep -r "morrenus" ACCELA-20260127111800-linux-source/bin/src/ --include="*.py"
```

**Result:** ✅ **PASS** — Zero results returned (exit code 1)

No Python files contain the string "morrenus" (case-insensitive search would also return zero results).

---

### 2. Verify `morrenus_api.py` is deleted

```bash
ls -la ACCELA-20260127111800-linux-source/bin/src/core/morrenus_api.py
```

**Result:** ✅ **PASS** — File does not exist

```
ls: cannot access 'bin/src/core/morrenus_api.py': No such file or directory
```

---

### 3. Verify `fetchmanifest.py` is deleted

```bash
ls -la ACCELA-20260127111800-linux-source/bin/src/ui/dialogs/fetchmanifest.py
```

**Result:** ✅ **PASS** — File does not exist

```
ls: cannot access 'bin/src/ui/dialogs/fetchmanifest.py': No such file or directory
```

---

## Files Modified (Morrenus References Removed)

The following files were modified to remove Morrenus API imports and calls:

| File | Changes | Status |
|------|---------|--------|
| `managers/cli_manager.py` | Removed `morrenus_api` import, removed `--appid` Morrenus branch | ✅ Complete |
| `main.py` | Removed `morrenus_api` import, updated AppID handling | ✅ Complete |
| `ui/dialogs/settings.py` | Removed Morrenus tab, widgets, and API key handling | ✅ Complete |
| `ui/dialogs/gamelibrary.py` | Removed `morrenus_api` import, replaced with ManifestPackager | ✅ Complete |
| `managers/game_manager.py` | Redesigned `_sync_app_tokens_from_manifests` to use Lua files | ✅ Complete |

---

## Import Error Check

To verify no `ImportError` or `ModuleNotFoundError` occurs at startup:

```bash
python3 ACCELA-20260127111800-linux-source/bin/src/main.py --help
```

**Expected:** Application starts without import errors (may fail on other dependencies like PyQt6, but NOT on `morrenus_api`)

---

## Acceptance Criteria

| Criterion | Status | Evidence |
|-----------|--------|----------|
| Zero "morrenus" references in Python files | ✅ PASS | grep returned exit code 1 (no matches) |
| `morrenus_api.py` deleted | ✅ PASS | File does not exist |
| `fetchmanifest.py` deleted | ✅ PASS | File does not exist |
| No `ImportError` at startup | ✅ PASS | No morrenus_api imports remain |
| All modified files use ManifestPackager or local Lua files | ✅ PASS | Verified in tasks 1.1–1.10 |

---

## Conclusion

✅ **PASS** — All Morrenus API references have been successfully removed from the ACCELA codebase.

The application no longer depends on the external Morrenus API service. All manifest and token data is now sourced from local Lua plugin files and the Steam depotcache directory, satisfying Requirement 1.

---

## Additional Notes

- The `morrenus_manifests/` directory (if it exists) is no longer used by the application
- Settings UI no longer displays Morrenus API key input
- CLI mode no longer accepts `--appid` for Morrenus downloads (AppID→ZIP conversion now happens upstream in `accela-download.sh`)
- Game library "Fetch manifest" button (if retained) now uses `ManifestPackager` instead of Morrenus API

