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

install_safe_steam_wrapper() {
    local steam_root="$HOME/.local/share/Steam"
    local steam_launcher="$steam_root/steam.sh"
    local steam_backup="$steam_root/steam.sh.bak"
    local wrapper_dir="$HOME/.local/bin"
    local wrapper="$wrapper_dir/steam"
    local native_steam=""

    # Prefer known native Steam launchers and avoid resolving a wrapper that
    # this installer is replacing. Steam can be installed outside /usr/bin.
    local candidate
    for candidate in \
        /usr/bin/steam \
        /usr/local/bin/steam \
        "$HOME/.steam/steam/steam.sh" \
        "$HOME/.local/share/Steam/steam.sh"; do
        if [ -x "$candidate" ] && ! grep -qE 'SLSsteam/library-inject.so|INJECT_SLS|LD_AUDIT' "$candidate" 2>/dev/null; then
            native_steam="$candidate"
            break
        fi
    done

    if [ -z "$native_steam" ]; then
        native_steam="$(type -P steam || true)"
        if [ -n "$native_steam" ] && grep -qE 'SLSsteam/library-inject.so|INJECT_SLS|LD_AUDIT' "$native_steam" 2>/dev/null; then
            native_steam=""
        fi
    fi

    if [ -z "$native_steam" ]; then
        log_error "Native Steam launcher not found; cannot create the SLSsteam wrapper."
        return 1
    fi

    # Headcrab patches steam.sh with a shell-level LD_AUDIT wrapper. That
    # wrapper can terminate Steam before its native client starts. Keep the
    # Steam launcher native and apply the 32-bit audit libraries at the
    # outer command boundary instead.
    if [ -f "$steam_backup" ] && grep -qE 'INJECT_SLS|LD_AUDIT' "$steam_launcher"; then
        if ! grep -qE 'INJECT_SLS|LD_AUDIT' "$steam_backup"; then
            cp -f "$steam_launcher" "$steam_launcher.headcrab-wrapper.bak"
            cp -f "$steam_backup" "$steam_launcher"
            chmod 755 "$steam_launcher"
            log_info "Restored the native Steam launcher."
        fi
    fi

    mkdir -p "$wrapper_dir"
    if [ -e "$wrapper" ] && ! grep -q 'SLSsteam/library-inject.so' "$wrapper" 2>/dev/null; then
        mv -f "$wrapper" "$wrapper.pre-slssteam.bak"
    fi

    cat > "$wrapper" <<EOF
#!/bin/sh
exec env LD_AUDIT="$HOME/.local/share/SLSsteam/library-inject.so:$HOME/.local/share/SLSsteam/SLSsteam.so" "$native_steam" "\$@"
EOF
    chmod 755 "$wrapper"

    # Make the wrapper discoverable from fresh Bash login and interactive
    # shells. Desktop entries still use the explicit LD_AUDIT command above.
    local path_line='export PATH="$HOME/.local/bin:$PATH"'
    for profile in "$HOME/.profile" "$HOME/.bashrc"; do
        touch "$profile"
        if ! grep -Fqx "$path_line" "$profile" 2>/dev/null; then
            printf '\n%s\n' "$path_line" >> "$profile"
        fi
    done

    log_info "Installed the safe SLSsteam Steam wrapper at $wrapper."
}

install_bundled_slssteam() {
    local sls_dir="$SCRIPT_DIR/SLSsteam"

    if [ ! -f "$sls_dir/setup.sh" ]; then
        log_error "SLSsteam/setup.sh not found in the release directory."
        exit 1
    fi

    log_info "Installing the bundled SLSsteam payload..."
    chmod +x "$sls_dir/setup.sh"
    if ! (cd "$sls_dir" && ./setup.sh install); then
        log_error "Bundled SLSsteam installation failed."
        exit 1
    fi
    log_info "Bundled SLSsteam payload installed successfully."
}

find_accela_dir() {
    if [ -d "$SCRIPT_DIR/ACCELA" ]; then
        printf '%s\n' "$SCRIPT_DIR/ACCELA"
        return 0
    fi

    local candidate
    for candidate in "$SCRIPT_DIR"/ACCELA-*; do
        if [ -f "$candidate/ACCELAINSTALL" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

log_info "Starting full installation..."

# 1. Install ACCELA
log_info "Installing ACCELA..."
ACCELA_DIR="$(find_accela_dir || true)"
if [ -n "$ACCELA_DIR" ]; then
    cd "$ACCELA_DIR"
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
    log_error "ACCELA directory not found in the release archive."
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

# Headcrab may install its own SLSsteam build. Reinstall the release payload so
# every fresh device uses the tested SLSsteam binary shipped in this archive.
install_bundled_slssteam
if ! install_safe_steam_wrapper; then
    log_error "Steam wrapper installation failed."
    exit 1
fi

log_info "Full installation complete!"
echo -e "${GREEN}ACCELA, SLSsteam, and Headcrab have been installed to their respective locations in ~/.local/share/${NC}"
