# SLSsteam-ACCELA Integration — Testing Guide

This guide walks you through testing the integration from simple components to the full pipeline.

---

## Prerequisites

Before testing, ensure you have:

1. ✅ Steam installed on Linux (`~/.steam/steam` or `~/.local/share/Steam`)
2. ✅ At least one Lua plugin file in `$SteamRoot/config/stplug-in/` (e.g., `293780.lua`)
3. ✅ Python 3 installed
4. ✅ ACCELA source code at `ACCELA-20260127111800-linux-source/`
5. ✅ SLSsteam source code at `SLSsteam/`

---

## Testing Levels

We'll test in this order:
1. **Level 1**: ManifestPackager (standalone, no dependencies)
2. **Level 2**: SLSsteam Lua Scanner (requires SLSsteam build)
3. **Level 3**: Full Pipeline (requires Steam + SLSsteam + ACCELA)

---

## Level 1: Test ManifestPackager (Standalone)

This tests the core packaging logic without requiring Steam to be running.

### Step 1.1: Verify Steam Installation

```bash
# Check if Steam root exists
ls -la ~/.steam/steam/config/stplug-in/
# or
ls -la ~/.local/share/Steam/config/stplug-in/
```

**Expected:** Directory exists with `.lua` files inside.

### Step 1.2: Pick a Test AppID

```bash
# List available Lua plugins
ls ~/.steam/steam/config/stplug-in/*.lua

# Pick one, e.g., 293780.lua
# The AppID is the filename without .lua extension
```

Let's say you pick `293780.lua` — your test AppID is **293780**.

### Step 1.3: Test ManifestPackager with Existing Lua

```bash
cd ACCELA-20260127111800-linux-source/bin/src

# Run the packager
python3 scripts/manifest_packager.py 293780
```

**Expected output:**
```
[manifest_packager] WARNING: Manifest not found for depot XXXXX at .../depotcache/XXXXX_YYYYY.manifest, skipping
[manifest_packager] INFO: Packaged AppID 293780: /tmp/accela_pkg_xyz/293780.zip (lua + 0 manifest(s))
/tmp/accela_pkg_xyz/293780.zip
```

The last line is the ZIP path. Copy it.

### Step 1.4: Verify ZIP Contents

```bash
# Replace with your actual ZIP path from above
unzip -l /tmp/accela_pkg_xyz/293780.zip
```

**Expected output:**
```
Archive:  /tmp/accela_pkg_xyz/293780.zip
  Length      Date    Time    Name
---------  ---------- -----   ----
     1234  2026-04-22 10:30   293780.lua
---------                     -------
     1234                     1 file
```

✅ **Success!** The packager works. The ZIP contains the Lua file.

### Step 1.5: Test with Manifests (Optional)

If you want to test manifest collection, manually place a fake manifest:

```bash
# Get a depot ID from the Lua file
grep "setManifestid" ~/.steam/steam/config/stplug-in/293780.lua
# Example output: setManifestid(293781, "9207527406397102173")

# Create a fake manifest
echo "FAKE_MANIFEST_FOR_TESTING" > ~/.steam/steam/config/depotcache/293781_9207527406397102173.manifest

# Run packager again
python3 scripts/manifest_packager.py 293780

# Verify the manifest is included
unzip -l /tmp/accela_pkg_*/293780.zip
# Should now show: 293780.lua AND 293781_9207527406397102173.manifest
```

✅ **Success!** Manifest collection works.

### Step 1.6: Test Error Cases

```bash
# Test with non-existent AppID
python3 scripts/manifest_packager.py 999999
# Expected: ERROR log about missing Lua file, exit code 1

# Test with invalid AppID
python3 scripts/manifest_packager.py invalid
# Expected: ERROR log about invalid AppID format, exit code 1
```

✅ **Level 1 Complete!** ManifestPackager works correctly.

---

## Level 2: Test SLSsteam Lua Scanner

This requires building SLSsteam.

### Step 2.1: Build SLSsteam

```bash
cd SLSsteam
make clean
make
```

**Expected:** `SLSsteam.so` is created in the build output directory.

**Note:** If the build fails with missing 32-bit headers (`bits/c++config.h`), you need to install multilib support:

```bash
# Ubuntu/Debian
sudo apt-get install gcc-multilib g++-multilib

# Arch Linux
sudo pacman -S lib32-gcc-libs
```

### Step 2.2: Check SLSsteam Config

```bash
# Check if config exists
cat ~/.config/SLSsteam/config.yaml

# Look for AdditionalApps section
grep -A 10 "AdditionalApps:" ~/.config/SLSsteam/config.yaml
```

Note the current AppIDs in `AdditionalApps`.

### Step 2.3: Test Lua Scanner (Standalone)

The Lua scanner runs automatically when Steam starts with SLSsteam injected. To test it standalone, you can:

**Option A: Check the implementation**

```bash
# Verify the scanner function exists
grep -A 20 "scanLuaPluginsAndUpdateConfig" SLSsteam/src/config.cpp
```

**Option B: Inject SLSsteam and check logs**

This requires Steam to be running. We'll do this in Level 3.

---

## Level 3: Test Full Pipeline

This is the complete end-to-end test.

### Step 3.1: Prepare Test Environment

```bash
# 1. Backup SLSsteam config
cp ~/.config/SLSsteam/config.yaml ~/.config/SLSsteam/config.yaml.backup

# 2. Ensure test AppID is in AdditionalApps
# Edit ~/.config/SLSsteam/config.yaml and add your test AppID (e.g., 293780) to AdditionalApps if not present

# 3. Install accela-download helper script
sudo cp ACCELA-20260127111800-linux-source/bin/scripts/accela-download.sh /usr/local/bin/accela-download
sudo chmod +x /usr/local/bin/accela-download

# 4. Verify helper script works
which accela-download
# Expected: /usr/local/bin/accela-download
```

### Step 3.2: Start Steam with SLSsteam

```bash
# Method 1: Using LD_PRELOAD
LD_PRELOAD=/path/to/SLSsteam.so steam

# Method 2: If you have a launch script
# Edit your Steam launch script to include the LD_PRELOAD
```

**Check SLSsteam is loaded:**
- Steam should start normally
- Check Steam console for SLSsteam initialization messages

### Step 3.3: Test Lua Scanner

```bash
# After Steam starts, check if Lua scanner ran
grep "scanLuaPluginsAndUpdateConfig" ~/.steam/steam/logs/console_log.txt

# Check if AppIDs were added to config
grep -A 20 "AdditionalApps:" ~/.config/SLSsteam/config.yaml
```

**Expected:** All AppIDs from Lua plugins should be in `AdditionalApps`.

### Step 3.4: Test Download Hook

**Important:** Pick a test AppID that is:
- ✅ In `AdditionalApps` (unlocked)
- ❌ NOT legitimately owned in your Steam account

1. **Open Steam Library**
2. **Find the unlocked game** (it should show as available to download)
3. **Click the Download button**

**What should happen:**

1. **Immediate:** Steam console shows:
   ```
   Download_Hook: intercepted AppID 293780
   ```

2. **Within 1-2 seconds:** A new terminal window opens with ACCELA CLI

3. **ACCELA CLI shows:**
   ```
   [manifest_packager] INFO: Packaged AppID 293780: /tmp/accela_pkg_xyz/293780.zip (lua + N manifest(s))
   Processing ZIP: /tmp/accela_pkg_xyz/293780.zip
   ```

4. **ACCELA starts downloading** depot content via DepotDownloader

**If nothing happens:**

Check these common issues:

```bash
# 1. Is accela-download in PATH?
which accela-download

# 2. Is the helper script executable?
ls -la /usr/local/bin/accela-download

# 3. Check Steam console for errors
tail -f ~/.steam/steam/logs/console_log.txt | grep -i "download"

# 4. Check if fork() failed
grep "fork() failed" ~/.steam/steam/logs/console_log.txt
```

### Step 3.5: Verify Download Completed

After ACCELA finishes downloading:

```bash
# 1. Check game files exist
ls -la ~/.steam/steam/steamapps/common/<game_name>/

# 2. Check manifests were placed in depotcache
ls -la ~/.steam/steam/config/depotcache/ | grep <depot_id>

# 3. Check config.yaml was updated
grep -A 5 "AppTokens:" ~/.config/SLSsteam/config.yaml | grep 293780
```

✅ **Success!** Full pipeline works.

---

## Quick Test Script

Here's a script to test the ManifestPackager quickly:

```bash
#!/bin/bash
# quick_test.sh — Test ManifestPackager with all Lua plugins

cd ACCELA-20260127111800-linux-source/bin/src

echo "Testing ManifestPackager with all available Lua plugins..."
echo ""

for lua_file in ~/.steam/steam/config/stplug-in/*.lua; do
    if [ -f "$lua_file" ]; then
        appid=$(basename "$lua_file" .lua)
        echo "Testing AppID: $appid"
        python3 scripts/manifest_packager.py "$appid" 2>&1 | grep -E "(ERROR|INFO|WARNING)"
        echo ""
    fi
done

echo "Test complete!"
```

Save this as `quick_test.sh`, make it executable, and run:

```bash
chmod +x quick_test.sh
./quick_test.sh
```

---

## Troubleshooting

### Issue: "Cannot locate Steam root"

**Solution:**
```bash
# Check if Steam is installed
ls -la ~/.steam/steam
ls -la ~/.local/share/Steam

# If using Flatpak Steam
ls -la ~/.var/app/com.valvesoftware.Steam/data/Steam
```

### Issue: "Lua plugin not found"

**Solution:**
```bash
# Check if Lua plugins exist
ls -la ~/.steam/steam/config/stplug-in/

# If empty, you need to create Lua plugins first
# (This is outside the scope of this integration — Lua plugins are created by SLSsteam)
```

### Issue: "accela-download: command not found"

**Solution:**
```bash
# Install the helper script
sudo cp ACCELA-20260127111800-linux-source/bin/scripts/accela-download.sh /usr/local/bin/accela-download
sudo chmod +x /usr/local/bin/accela-download

# Or add to PATH
export PATH="$PATH:$(pwd)/ACCELA-20260127111800-linux-source/bin/scripts"
```

### Issue: Download Hook doesn't trigger

**Solution:**
```bash
# 1. Verify SLSsteam is loaded
grep "SLSsteam" ~/.steam/steam/logs/console_log.txt

# 2. Verify AppID is in AdditionalApps
grep -A 20 "AdditionalApps:" ~/.config/SLSsteam/config.yaml

# 3. Verify AppID is NOT owned
# (Check your Steam account — if you legitimately own the game, the hook won't trigger)

# 4. Check for fork() errors
grep "fork" ~/.steam/steam/logs/console_log.txt
```

---

## What to Test First

**Recommended testing order:**

1. ✅ **Start here:** Level 1 (ManifestPackager) — 5 minutes, no dependencies
2. ✅ **Then:** Build SLSsteam — 10 minutes
3. ✅ **Finally:** Level 3 (Full Pipeline) — 30 minutes, requires Steam restart

**Minimum viable test:**

If you just want to verify the integration works:

```bash
# 1. Test ManifestPackager
cd ACCELA-20260127111800-linux-source/bin/src
python3 scripts/manifest_packager.py 293780

# 2. If that works, the integration is functional
# The rest is just wiring it up to Steam
```

---

## Need Help?

Check the detailed test procedures:
- `ACCELA-20260127111800-linux-source/bin/src/tests/manual_manifest_placement_test.md`
- `ACCELA-20260127111800-linux-source/bin/src/tests/e2e_integration_test.md`
- `ACCELA-20260127111800-linux-source/bin/src/tests/error_logging_audit.md`

