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

# 1. Install SLSsteam
log_info "Installing SLSsteam..."
if [ -d "SLSsteam" ]; then
    cd SLSsteam
    if [ -f "./setup.sh" ]; then
        chmod +x ./setup.sh
        ./setup.sh install
        if [ $? -eq 0 ]; then
            log_info "SLSsteam installed successfully."
        else
            log_error "SLSsteam installation failed."
            exit 1
        fi
    else
        log_error "SLSsteam/setup.sh not found."
        exit 1
    fi
    cd "$SCRIPT_DIR"
else
    log_error "SLSsteam directory not found in prod-release."
    exit 1
fi

# 2. Install ACCELA
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

log_info "Full installation complete!"
echo -e "${GREEN}SLSsteam and ACCELA have been installed to their respective locations in ~/.local/share/${NC}"