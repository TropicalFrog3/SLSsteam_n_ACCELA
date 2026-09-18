#!/bin/bash

# Exit on error
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SLS_DIR="$REPO_ROOT/SLSsteam"
ACCELA_DIR="$REPO_ROOT/ACCELA-20260127111800-linux-source"
PROD_SLS_DIR="$REPO_ROOT/prod-release/SLSsteam"
PROD_ACCELA_DIR="$REPO_ROOT/prod-release/ACCELA"

echo "Building SLSsteam..."
cd "$SLS_DIR"
make

echo "Updating prod-release for SLSsteam..."
mkdir -p "$PROD_SLS_DIR/bin"
mkdir -p "$PROD_SLS_DIR/res"
cp "$SLS_DIR/bin/SLSsteam.so" "$PROD_SLS_DIR/bin/"
cp "$SLS_DIR/bin/library-inject.so" "$PROD_SLS_DIR/bin/"
cp "$SLS_DIR/res/updates.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/config.yaml" "$PROD_SLS_DIR/res/"
[ -f "$SLS_DIR/res/config.diff.yaml" ] && cp "$SLS_DIR/res/config.diff.yaml" "$PROD_SLS_DIR/res/"
[ -f "$SLS_DIR/res/cache.diff.yaml" ] && cp "$SLS_DIR/res/cache.diff.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/version" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/update-all.sh" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/setup.sh" "$PROD_SLS_DIR/"
rm -rf "$PROD_SLS_DIR/tools" "$PROD_SLS_DIR/docs"
cp -r "$SLS_DIR/tools" "$PROD_SLS_DIR/"
cp -r "$SLS_DIR/docs" "$PROD_SLS_DIR/"

echo "Updating prod-release for ACCELA..."
mkdir -p "$PROD_ACCELA_DIR/bin"
cp "$ACCELA_DIR/ACCELAINSTALL" "$PROD_ACCELA_DIR/"
cp "$ACCELA_DIR/bin/accela.png" "$PROD_ACCELA_DIR/bin/"
cp "$ACCELA_DIR/bin/requirements.txt" "$PROD_ACCELA_DIR/bin/"
cp "$ACCELA_DIR/bin/run.sh" "$PROD_ACCELA_DIR/bin/"

# Overwrite directories cleanly
rm -rf "$PROD_ACCELA_DIR/bin/scripts"
cp -r "$ACCELA_DIR/bin/scripts" "$PROD_ACCELA_DIR/bin/"
rm -rf "$PROD_ACCELA_DIR/bin/src"
cp -r "$ACCELA_DIR/bin/src" "$PROD_ACCELA_DIR/bin/"

chmod +x "$REPO_ROOT/uninstall.sh"
cp "$REPO_ROOT/install.sh" "$REPO_ROOT/prod-release/"
cp "$REPO_ROOT/uninstall.sh" "$REPO_ROOT/prod-release/"
if [ -d "$REPO_ROOT/release/SLSsteam-n-ACCELA" ]; then
    cp "$REPO_ROOT/uninstall.sh" "$REPO_ROOT/release/SLSsteam-n-ACCELA/"
fi

echo "Generating update-manifest.yaml..."
cd "$REPO_ROOT/prod-release"
VER_STR=$(grep '^Version:' SLSsteam/res/version 2>/dev/null | cut -d' ' -f2 | xargs || echo '1.0.0')
printf "version: \"%s\"\nfiles:\n" "$VER_STR" > update-manifest.yaml
find SLSsteam ACCELA install.sh uninstall.sh -type f | sort | while read -r f; do
    hash=$(sha256sum "$f" | awk '{print $1}')
    printf "  \"%s\": \"%s\"\n" "$f" "$hash" >> update-manifest.yaml
done

echo "Packaging SLSsteam-n-ACCELA.zip..."
rm -f "$REPO_ROOT/SLSsteam-n-ACCELA.zip"
zip -q -r "$REPO_ROOT/SLSsteam-n-ACCELA.zip" install.sh uninstall.sh update-manifest.yaml SLSsteam ACCELA

echo "Prod-release and SLSsteam-n-ACCELA.zip updated successfully!"
