#include "accelapath.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace CppAccela::Path
{
    // ── Internal helpers ──────────────────────────────────────────────────────

    static const char* homeDir()
    {
        const char* home = getenv("HOME");
        return home ? home : "/tmp";
    }

    // ── Public API ────────────────────────────────────────────────────────────

    std::string findSteamRoot()
    {
        const char* home = homeDir();

        const fs::path candidates[] = {
            fs::path(home) / ".local" / "share" / "Steam",
            fs::path(home) / ".steam" / "steam",
            fs::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
        };

        for (const auto& path : candidates)
        {
            // steamui/ is a reliable marker for a valid Steam install tree
            if (fs::exists(path / "steamui"))
                return path.string();
        }
        return {};
    }

    std::string stplugDir()
    {
        const std::string root = findSteamRoot();
        if (root.empty()) return {};

        const fs::path dir = fs::path(root) / "config" / "stplug-in";
        fs::create_directories(dir);
        return dir.string();
    }

    std::string depotcacheDir()
    {
        const std::string root = findSteamRoot();
        if (root.empty()) return {};

        const fs::path dir = fs::path(root) / "config" / "depotcache";
        fs::create_directories(dir);
        return dir.string();
    }

    std::string accelaRunSh()
    {
        const char* home = homeDir();

        // 1. Production install location
        fs::path installed = fs::path(home) / ".local" / "share" / "ACCELA" / "run.sh";
        if (fs::exists(installed))
            return installed.string();

        // 2. No fallback possible from C++ (we don't know the script's real dir),
        //    so return empty — callers must handle this gracefully.
        return {};
    }

} // namespace CppAccela::Path
