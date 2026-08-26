#!/bin/bash

# prod-release/install.sh
# Master installation script for SLSsteam and ACCELA

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd "$SCRIPT_DIR"

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_info "Starting full installation..."

# 1. Install ACCELA
log_info "Installing ACCELA..."
if [ -d "ACCELA" ]; then
    cd ACCELA
    if [ -f "./ACCELAINSTALL" ]; then
        chmod +x ./ACCELAINSTALL
        ./ACCELAINSTALL
        if [ $? -eq 0 ]; then
            log_info "ACCELA installed successfully."
        else
            log_error "ACCELA installation failed."
            exit 1
        fi
    else
        log_error "ACCELA/ACCELAINSTALL not found."
        exit 1
    fi
    cd "$SCRIPT_DIR"
else
    log_error "ACCELA directory not found in prod-release."
    exit 1
fi

# 2. Install Headcrab (SLSsteam and Steam client integration)
log_info "Installing Headcrab and SLSsteam..."
HEADCRAB_URL="https://raw.githubusercontent.com/Deadboy666/h3adcr-b/main/headcrab.sh"
HEADCRAB_SCRIPT="$(mktemp "${TMPDIR:-/tmp}/headcrab.XXXXXX.sh")"
trap 'rm -f "$HEADCRAB_SCRIPT"' EXIT

if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 --retry-delay 2 "$HEADCRAB_URL" -o "$HEADCRAB_SCRIPT"
elif command -v wget >/dev/null 2>&1; then
    wget -q --tries=3 -O "$HEADCRAB_SCRIPT" "$HEADCRAB_URL"
else
    log_error "Neither curl nor wget is available to download Headcrab."
    exit 1
fi

chmod +x "$HEADCRAB_SCRIPT"
if bash "$HEADCRAB_SCRIPT"; then
    log_info "Headcrab and SLSsteam installed successfully."
else
    log_error "Headcrab/SLSsteam installation failed."
    exit 1
fi

log_info "Full installation complete!"
echo -e "${GREEN}ACCELA, SLSsteam, and Headcrab have been installed to their respective locations in ~/.local/share/${NC}"
