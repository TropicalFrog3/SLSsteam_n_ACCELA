#!/bin/bash
set -euo pipefail

REPO="TropicalFrog3/SLSsteam_n_ACCELA"
MANIFEST_URL="https://raw.githubusercontent.com/$REPO/main/SLSsteam/res/version"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/slssteam-full-update.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if command -v curl >/dev/null 2>&1; then
    manifest="$(curl -fsSL --retry 3 --retry-delay 2 "$MANIFEST_URL")"
else
    manifest="$(wget -qO- "$MANIFEST_URL")"
fi

version="$(printf '%s\n' "$manifest" | sed -n 's/^Version: *//p' | head -n 1)"
if [ -z "$version" ]; then
    exit 1
fi

archive="$TMP_DIR/SLSsteam-n-ACCELA.zip"
url="https://github.com/$REPO/releases/download/$version/SLSsteam-n-ACCELA.zip"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --retry-delay 2 "$url" -o "$archive"
else
    wget -q --tries=3 -O "$archive" "$url"
fi

unzip -q -o "$archive" -d "$TMP_DIR/extracted"
bash "$TMP_DIR/extracted/install.sh"
