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
cp "$SLS_DIR/bin/SLSsteam.so" "$PROD_SLS_DIR/bin/"
cp "$SLS_DIR/bin/library-inject.so" "$PROD_SLS_DIR/bin/"
cp "$SLS_DIR/res/updates.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/config.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/version.txt" "$PROD_SLS_DIR/res/"

echo "Updating prod-release for ACCELA..."
cp "$ACCELA_DIR/ACCELAINSTALL" "$PROD_ACCELA_DIR/"
cp "$ACCELA_DIR/bin/accela.png" "$PROD_ACCELA_DIR/bin/"
cp "$ACCELA_DIR/bin/requirements.txt" "$PROD_ACCELA_DIR/bin/"
cp "$ACCELA_DIR/bin/run.sh" "$PROD_ACCELA_DIR/bin/"

# Overwrite directories cleanly
rm -rf "$PROD_ACCELA_DIR/bin/scripts"
cp -r "$ACCELA_DIR/bin/scripts" "$PROD_ACCELA_DIR/bin/"
rm -rf "$PROD_ACCELA_DIR/bin/src"
cp -r "$ACCELA_DIR/bin/src" "$PROD_ACCELA_DIR/bin/"

echo "Prod-release updated successfully!"
