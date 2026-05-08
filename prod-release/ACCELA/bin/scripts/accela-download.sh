#!/bin/bash
# accela-download.sh — called by SLSsteam Download_Hook
# Usage: accela-download <appid>
#
# Step 1: Package Lua plugin + depot manifests into a ZIP
# Step 2: Launch ACCELA CLI with the pre-built ZIP

set -euo pipefail

APPID="$1"
shift

# Locate ACCELA's run.sh:
#   1. Production install path (~/.local/share/ACCELA/)
#   2. Fallback: relative to this script's real location (source/dev layout)
ACCELA_INSTALL="$HOME/.local/share/ACCELA"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

if [ -f "$ACCELA_INSTALL/run.sh" ]; then
    ACCELA_RUN="$ACCELA_INSTALL/run.sh"
elif [ -f "$SCRIPT_DIR/../run.sh" ]; then
    ACCELA_RUN="$SCRIPT_DIR/../run.sh"
else
    echo "[accela-download] ERROR: Cannot find ACCELA run.sh (checked $ACCELA_INSTALL and $SCRIPT_DIR/..)" >&2
    exit 1
fi

log() { echo "[accela-download] $*" >&2; }

# ── Step 1: Find Steam root ──────────────────────────────────────────────────
STEAM_ROOT=""
for candidate in \
    "$HOME/.steam/steam" \
    "$HOME/.local/share/Steam" \
    "$HOME/.var/app/com.valvesoftware.Steam/data/Steam"; do
    if [ -d "$candidate/steamapps" ]; then
        STEAM_ROOT="$(readlink -f "$candidate")"
        break
    fi
done

if [ -z "$STEAM_ROOT" ]; then
    log "ERROR: Cannot locate Steam root (searched ~/.steam/steam, ~/.local/share/Steam, Flatpak path)"
    exit 1
fi
log "Steam root: $STEAM_ROOT"

# ── Step 2: Locate the Lua plugin ────────────────────────────────────────────
LUA_PATH="$STEAM_ROOT/config/stplug-in/${APPID}.lua"
if [ ! -f "$LUA_PATH" ]; then
    log "ERROR: Lua plugin not found for AppID $APPID at $LUA_PATH"
    exit 1
fi
log "Found Lua plugin: $LUA_PATH"

# ── Step 3: Validate the Lua file ────────────────────────────────────────────
if ! grep -qi 'addappid(' "$LUA_PATH"; then
    log "ERROR: Malformed Lua plugin for AppID $APPID — no addappid() entries"
    exit 1
fi

# ── Step 4: Parse setManifestid(depotId, "manifestGid") pairs ────────────────
#   Extracts lines like: setManifestid( 293781 , "9207527406397102173" )
#   Outputs:  depotId manifestGid  per line
MANIFEST_PAIRS=$(grep -oiP 'setManifestid\(\s*\K\d+\s*,\s*"\d+"' "$LUA_PATH" \
    | sed 's/[[:space:]]*,[[:space:]]*"/ /; s/"//' || true)

# ── Step 5: Collect manifest files from depotcache ───────────────────────────
DEPOTCACHE="$STEAM_ROOT/config/depotcache"
MANIFEST_FILES=()
MISSING_DEPOTS=()

while IFS=' ' read -r depot_id manifest_gid; do
    [ -z "$depot_id" ] && continue
    manifest_file="$DEPOTCACHE/${depot_id}_${manifest_gid}.manifest"
    if [ -f "$manifest_file" ]; then
        MANIFEST_FILES+=("$manifest_file")
    else
        log "Manifest not found locally for depot $depot_id (${depot_id}_${manifest_gid}.manifest)"
        MISSING_DEPOTS+=("$depot_id")
    fi
done <<< "$MANIFEST_PAIRS"

# ── Step 5b: Fetch missing manifest IDs from manifest.steam.run API ──────────
if [ ${#MISSING_DEPOTS[@]} -gt 0 ]; then
    log "Fetching manifest info from manifest.steam.run API for ${#MISSING_DEPOTS[@]} missing depot(s)..."
    API_RESPONSE=$(curl -sS --connect-timeout 10 --max-time 15 \
        "https://manifest.steam.run/api/depot/${APPID}" 2>&1) || true

    if [ -n "$API_RESPONSE" ] && echo "$API_RESPONSE" | grep -q '"depots"'; then
        log "API response received, resolving missing manifests..."
        PLACEHOLDER_DIR=$(mktemp -d --tmpdir accela_manifests_XXXXXX)

        for depot_id in "${MISSING_DEPOTS[@]}"; do
            # Extract manifestid for this depot from the JSON response
            # JSON format: {"depotid":293781,"manifestid":"9207527406397102173",...}
            api_manifest_gid=$(echo "$API_RESPONSE" \
                | grep -oP "\"depotid\"\\s*:\\s*${depot_id}[^}]*\"manifestid\"\\s*:\\s*\"\\K[0-9]+" || true)

            if [ -n "$api_manifest_gid" ]; then
                placeholder="$PLACEHOLDER_DIR/${depot_id}_${api_manifest_gid}.manifest"
                # Create an empty placeholder — DepotDownloaderMod will fetch the
                # real manifest from Steam's CDN using the manifest ID.
                touch "$placeholder"
                MANIFEST_FILES+=("$placeholder")
                log "Created placeholder manifest for depot $depot_id (GID: $api_manifest_gid)"
            else
                log "WARNING: API did not return manifest info for depot $depot_id, skipping"
            fi
        done
    else
        log "WARNING: Could not fetch manifest info from API (response: ${API_RESPONSE:0:200})"
        log "Continuing with ${#MANIFEST_FILES[@]} locally found manifest(s)..."
    fi
fi

log "Found ${#MANIFEST_FILES[@]} manifest file(s) (local + API-resolved)"

# ── Step 6: Assemble ZIP ─────────────────────────────────────────────────────
TMP_DIR=$(mktemp -d --tmpdir accela_pkg_XXXXXX)
ZIP_PATH="$TMP_DIR/${APPID}.zip"

# Create the ZIP with the Lua file + all found manifests (stored, no compression)
if [ ${#MANIFEST_FILES[@]} -gt 0 ]; then
    zip -j -0 "$ZIP_PATH" "$LUA_PATH" "${MANIFEST_FILES[@]}" >/dev/null 2>&1
else
    log "ERROR: Cannot complete the goal because of missing manifest files."
    rm -rf "$TMP_DIR"
    exit 1
fi

if [ ! -f "$ZIP_PATH" ]; then
    log "ERROR: ZIP assembly failed for AppID $APPID"
    rm -rf "$TMP_DIR"
    exit 1
fi

log "Packaged: $ZIP_PATH (lua + ${#MANIFEST_FILES[@]} manifest(s))"

set +e
"$ACCELA_RUN" -cli "$ZIP_PATH" "$@"
EXIT_CODE=$?
set -e

# ACCELA may exit with code 13 if SLScheevo fails (e.g., no username for achievements).
# This is a non-critical post-processing error, the download itself succeeds.
if [ $EXIT_CODE -eq 13 ]; then
    log "WARNING: ACCELA returned exit code 13 (SLScheevo achievement generation failed). Ignoring as non-critical."
    exit 0
fi

exit $EXIT_CODE
