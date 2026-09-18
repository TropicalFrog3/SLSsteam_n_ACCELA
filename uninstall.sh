#!/usr/bin/env bash
#
# uninstall.sh
# Complete Uninstallation Script for SLSsteam, ACCELA, and Headcrab
#
set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if GUI (Zenity) is available and usable
HAS_ZENITY=0
if command -v zenity >/dev/null 2>&1 && [ -n "${DISPLAY:-${WAYLAND_DISPLAY:-}}" ]; then
    HAS_ZENITY=1
fi

# Ask for confirmation
confirm_uninstall() {
    if [ "${SLS_UNINSTALL_AUTO_ACCEPT:-0}" = "1" ]; then
        return 0
    fi
    if [ "$HAS_ZENITY" -eq 1 ]; then
        zenity --question \
            --title="SLSsteam & ACCELA Uninstaller" \
            --text="Are you sure you want to completely uninstall SLSsteam, ACCELA, and associated components?" \
            --ok-label="Uninstall" --cancel-label="Cancel" \
            --width=450 2>/dev/null
        return $?
    else
        echo -e "${YELLOW}====================================================${NC}"
        echo -e "${YELLOW}      SLSsteam & ACCELA Complete Uninstaller        ${NC}"
        echo -e "${YELLOW}====================================================${NC}"
        read -r -p "Are you sure you want to completely uninstall SLSsteam and ACCELA? [y/N]: " response
        case "$response" in
            [yY][eE][sS]|[yY])
                return 0
                ;;
            *)
                return 1
                ;;
        esac
    fi
}

# Ask whether to remove user configurations
ask_remove_configs() {
    if [ "${SLS_UNINSTALL_REMOVE_CONFIGS:-0}" = "1" ]; then
        return 0 # Delete configs
    fi
    if [ "${SLS_UNINSTALL_KEEP_CONFIGS:-0}" = "1" ]; then
        return 1 # Keep configs
    fi
    if [ "$HAS_ZENITY" -eq 1 ]; then
        if zenity --question \
            --title="Remove User Configurations?" \
            --text="Do you also want to remove your user configuration and saved data?\n\n- ~/.config/SLSsteam\n- ~/.config/Tachibana Labs\n\n(Click 'No' to preserve your settings and game AppID configurations)" \
            --ok-label="Keep Configs" --cancel-label="Delete Configs" \
            --width=500 2>/dev/null; then
            return 1 # Keep configs
        else
            return 0 # Delete configs
        fi
    else
        echo ""
        read -r -p "Do you want to delete your user configurations (~/.config/SLSsteam, ~/.config/Tachibana Labs)? [y/N]: " response
        case "$response" in
            [yY][eE][sS]|[yY])
                return 0
                ;;
            *)
                return 1
                ;;
        esac
    fi
}

if ! confirm_uninstall; then
    log_info "Uninstallation cancelled by user."
    exit 0
fi

REMOVE_CONFIGS=0
if ask_remove_configs; then
    REMOVE_CONFIGS=1
fi

log_info "Starting uninstallation process..."

# ==============================================================================
# Category 1: Application Data & Binaries
# ==============================================================================
log_info "Removing application directories..."

APP_DIRS=(
    "$HOME/.local/share/SLSsteam"
    "$HOME/.local/share/CloudRedirect"
    "$HOME/.headcrab"
    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam"
)

for dir in "${APP_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        rm -rf "$dir"
        log_info "Removed directory: $dir"
    fi
done

# Clean ACCELA application files, but explicitly preserve the manifest directory
ACCELA_DIR="$HOME/.local/share/ACCELA"
if [ -d "$ACCELA_DIR" ]; then
    if [ -d "$ACCELA_DIR/morrenus_manifests" ]; then
        find "$ACCELA_DIR" -mindepth 1 -maxdepth 1 ! -name "morrenus_manifests" -exec rm -rf {} +
        log_info "Cleaned ACCELA binaries while preserving manifest directory ($ACCELA_DIR/morrenus_manifests) intact."
    else
        rm -rf "$ACCELA_DIR"
        log_info "Removed directory: $ACCELA_DIR"
    fi
fi

# ==============================================================================
# Category 2: System & Steam Integration
# ==============================================================================
log_info "Restoring Steam launchers and wrappers..."

# 1. ~/.local/bin/steam
STEAM_WRAPPER="$HOME/.local/bin/steam"
STEAM_WRAPPER_BAK="$HOME/.local/bin/steam.pre-slssteam.bak"

if [ -f "$STEAM_WRAPPER_BAK" ]; then
    mv -f "$STEAM_WRAPPER_BAK" "$STEAM_WRAPPER"
    chmod 755 "$STEAM_WRAPPER"
    log_info "Restored original Steam wrapper from $STEAM_WRAPPER_BAK"
elif [ -f "$STEAM_WRAPPER" ]; then
    # Only remove if it contains SLSsteam references
    if grep -qE 'SLSsteam|library-inject|LD_AUDIT' "$STEAM_WRAPPER" 2>/dev/null; then
        rm -f "$STEAM_WRAPPER"
        log_info "Removed SLSsteam Steam wrapper: $STEAM_WRAPPER"
    fi
fi

# 2. Native steam.sh restoration if patched by headcrab
for steam_root in "$HOME/.local/share/Steam" "$HOME/.steam/steam"; do
    launcher="$steam_root/steam.sh"
    bak="$steam_root/steam.sh.bak"
    headcrab_bak="$steam_root/steam.sh.headcrab-wrapper.bak"

    if [ -f "$headcrab_bak" ]; then
        mv -f "$headcrab_bak" "$launcher"
        chmod 755 "$launcher"
        log_info "Restored native steam.sh from $headcrab_bak"
    elif [ -f "$bak" ] && [ -f "$launcher" ]; then
        if grep -qE 'INJECT_SLS|LD_AUDIT' "$launcher" 2>/dev/null; then
            mv -f "$bak" "$launcher"
            chmod 755 "$launcher"
            log_info "Restored native steam.sh from $bak"
        fi
    fi
done

# 3. Helper binaries and symlinks
if [ -e "$HOME/.local/bin/accela-download" ]; then
    rm -f "$HOME/.local/bin/accela-download"
    log_info "Removed helper: $HOME/.local/bin/accela-download"
fi

# 4. Steam plugin and manifest directories (luas and depot manifests)
# NOTE: The luas and manifest directories (stplug-in, depotcache) are strictly preserved with NO deletion and NO changes.
log_info "Preserving luas and manifest directories intact (e.g. stplug-in and config/depotcache)."

# 5. Desktop entries & Icons
DESKTOP_DIR="$HOME/.local/share/applications"
if [ -d "$DESKTOP_DIR" ]; then
    for entry in "accela.desktop" "headcrab.desktop"; do
        if [ -f "$DESKTOP_DIR/$entry" ]; then
            rm -f "$DESKTOP_DIR/$entry"
            log_info "Removed desktop entry: $DESKTOP_DIR/$entry"
        fi
    done

    # Check steam.desktop & steam-native.desktop in local share
    for steam_desk in "steam.desktop" "steam-native.desktop"; do
        target="$DESKTOP_DIR/$steam_desk"
        if [ -f "$target" ] && grep -qE 'SLSsteam|library-inject|LD_AUDIT' "$target" 2>/dev/null; then
            rm -f "$target"
            log_info "Removed modified desktop entry: $target"
        fi
    done
fi

# Remove icons
rm -f "$HOME/.local/share/icons/hicolor/"*/apps/accela.png 2>/dev/null || true
rm -f "$HOME/.local/share/icons/hicolor/"*/apps/headcrab.png 2>/dev/null || true
rm -f "$HOME/.local/share/pixmaps/accela.png" 2>/dev/null || true

# 6. Flatpak environment overrides
if command -v flatpak >/dev/null 2>&1; then
    flatpak override --user --unset-env=LD_AUDIT com.valvesoftware.Steam 2>/dev/null || true
    flatpak override --user --unset-env=SHARED_LIBRARY_GUARD com.valvesoftware.Steam 2>/dev/null || true
    log_info "Reset Flatpak Steam environment overrides."
fi

# 7. Root tracking & log files
TRACKING_FILES=(
    "$HOME/.SLSsteam.log"
    "$HOME/.SLSsteam.json"
    "$HOME/.SLSsteam.installed"
    "$HOME/.SLSsteam.onlinefix"
    "$HOME/.SLSsteam.autocrack"
    "$HOME/.config/fish/SLSsteam.fish"
    "/tmp/SLSsteam.API"
)

for track in "${TRACKING_FILES[@]}"; do
    if [ -e "$track" ]; then
        rm -f "$track"
        log_info "Removed tracking/log file: $track"
    fi
done

# ==============================================================================
# Category 3: User Configurations (Conditional)
# ==============================================================================
if [ "$REMOVE_CONFIGS" -eq 1 ]; then
    log_info "Removing user configuration and cache files..."
    CONFIG_DIRS=(
        "$HOME/.config/SLSsteam"
        "$HOME/.config/Tachibana Labs"
        "$HOME/.local/share/SLSsteam/cache"
        "$HOME/.local/share/SLSsteam/cache_backups"
        "$HOME/.cache/SLSsteam"
        "$HOME/.cache/ACCELA"
    )
    for cdir in "${CONFIG_DIRS[@]}"; do
        if [ -d "$cdir" ]; then
            rm -rf "$cdir"
            log_info "Removed configuration/cache directory: $cdir"
        fi
    done
else
    log_info "Preserving user configurations at ~/.config/SLSsteam and ~/.config/Tachibana Labs."
fi

# ==============================================================================
# Refresh desktop and icon database
# ==============================================================================
log_info "Refreshing system caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

if [ -z "${XDG_CURRENT_DESKTOP:-}" ] || [[ "$XDG_CURRENT_DESKTOP" != *"KDE"* ]]; then
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    command -v gtk4-update-icon-cache >/dev/null 2>&1 && gtk4-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
fi

log_info "Uninstallation completed successfully!"

if [ "$HAS_ZENITY" -eq 1 ] && [ "${SLS_UNINSTALL_AUTO_ACCEPT:-0}" != "1" ]; then
    zenity --info \
        --title="SLSsteam & ACCELA Uninstaller" \
        --text="Uninstallation completed successfully!\n\nAll components have been cleanly removed." \
        --width=350 2>/dev/null
fi
