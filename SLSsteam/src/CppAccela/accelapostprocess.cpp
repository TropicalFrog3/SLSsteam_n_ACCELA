#include "accelapostprocess.hpp"

// NOTE: Do NOT include config.hpp or use g_config here.
// This file runs inside the accela-helper binary — no globals, no mutexes.
// Use fprintf(stderr,...) for logging and resolve all paths from the Context struct.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include <set>
#include <sys/stat.h>
#include <unistd.h>

// Child-safe log: direct stderr write, no locks
#define PPLOG(fmt, ...) \
    fprintf(stderr, "[AccelaPostProcess] " fmt __VA_OPT__(,) __VA_ARGS__)

namespace fs = std::filesystem;

namespace CppAccela::PostProcess
{
    // ── Helpers ───────────────────────────────────────────────────────────────

    /** Produce a filesystem-safe name: keep [A-Za-z0-9_\s-], collapse, underscore. */
    static std::string sanitiseName(const std::string& name)
    {
        std::string s;
        s.reserve(name.size());
        for (unsigned char c : name)
        {
            if (std::isalnum(c) || c == '_' || c == '-' || c == ' ')
                s.push_back(static_cast<char>(c));
        }
        size_t start = s.find_first_not_of(' ');
        if (start == std::string::npos) return {};
        size_t end = s.find_last_not_of(' ');
        s = s.substr(start, end - start + 1);
        // Do not replace spaces with underscores to match Steam's directory naming exactly.
        // std::replace(s.begin(), s.end(), ' ', '_');
        return s;
    }

    static std::string installFolderName(const LuaParser::ParseResult& lua)
    {
        if (const char* envDir = getenv("ACCELA_INSTALLDIR"); envDir && envDir[0] != '\0')
        {
            return envDir;
        }

        std::string name = sanitiseName(lua.gameName);
        if (name.empty())
            name = "App_" + lua.appId;
        return name;
    }

    // ── Step 1: ACF ───────────────────────────────────────────────────────────

    bool writeAcf(const Context& ctx)
    {
        const auto& lua      = ctx.lua;
        const auto& dlResult = ctx.dlResult;

        if (lua.appId.empty() || ctx.destPath.empty()) return false;

        const std::string installDir = installFolderName(lua);
        const std::string steamappsDir = ctx.destPath + "/steamapps";
        const std::string acfPath =
            steamappsDir + "/appmanifest_" + lua.appId + ".acf";

        // Build InstalledDepots block.
        // Only include depot IDs that actually have a manifest GID AND a key
        // (i.e. were actually downloaded by DepotDownloaderMod).
        // The main appid is NOT a depot — skip it.
        std::ostringstream depotsBuf;
        bool hasWindowsDepots = false;
        uint64_t totalSize    = 0;

        for (const auto& [depotId, info] : lua.depots)
        {
            // Must have been downloaded: needs both key and manifest GID
            if (info.manifestGid.empty() || info.key.empty()) continue;

            // Skip if depotId == main appId (that's not a depot entry)
            if (depotId == lua.appId) continue;

            // Detect Windows-only depots from description comment
            std::string descLow = info.description;
            std::transform(descLow.begin(), descLow.end(),
                           descLow.begin(), ::tolower);
            if (descLow.find("windows") != std::string::npos ||
                descLow.find("win")     != std::string::npos)
                hasWindowsDepots = true;

            uint64_t depotSize = 0;
            if (!info.sizeBytes.empty())
            {
                try { depotSize = std::stoull(info.sizeBytes); }
                catch (...) {}
            }
            totalSize += depotSize;

            depotsBuf
                << "\t\t\"" << depotId << "\"\n"
                << "\t\t{\n"
                << "\t\t\t\"manifest\"\t\t\"" << info.manifestGid    << "\"\n"
                << "\t\t\t\"size\"\t\t\""     << std::to_string(depotSize) << "\"\n"
                << "\t\t}\n";
        }

        // StateFlags=4: fully installed.
        // Steam EAppState bitmask: 1=uninstalled, 2=update required, 4=fully installed,
        // 8=awaiting update, 16=downloading, 32=staging.
        // We write 4 (fully installed, no pending update) to prevent Steam from
        // showing the game as "Update Required" and failing with "content still
        // encrypted" when the user tries to update through Steam's normal pipeline.
        const std::string stateFlags = "4";

        // SizeOnDisk: prefer sum from depot size fields; fall back to dl result
        const uint64_t sizeOnDiskVal =
            (totalSize > 0) ? totalSize : dlResult.totalBytes;
        const std::string sizeOnDisk = std::to_string(sizeOnDiskVal);

        const std::string depotBlock = depotsBuf.str();
        const std::string installedDepots =
            depotBlock.empty()
            ? "\t\"InstalledDepots\"\n\t{\n\t}"
            : "\t\"InstalledDepots\"\n\t{\n" + depotBlock + "\t}";

        // Platform config: Windows depots on Linux → add Proton override
        std::string platformConfig;
        if (hasWindowsDepots)
        {
            PPLOG("Windows depots detected — adding Proton compat config\n");
            platformConfig =
                "\t\"UserConfig\"\n\t{\n"
                "\t\t\"platform_override_dest\"\t\t\"linux\"\n"
                "\t\t\"platform_override_source\"\t\t\"windows\"\n"
                "\t}\n"
                "\t\"MountedConfig\"\n\t{\n"
                "\t\t\"platform_override_dest\"\t\t\"linux\"\n"
                "\t\t\"platform_override_source\"\t\t\"windows\"\n"
                "\t}";
        }
        else
        {
            platformConfig =
                "\t\"UserConfig\"\n\t{\n\t}\n"
                "\t\"MountedConfig\"\n\t{\n\t}";
        }

        std::ostringstream acf;
        acf << "\"AppState\"\n{\n"
            << "\t\"appid\"\t\t\""        << lua.appId    << "\"\n"
            << "\t\"Universe\"\t\t\"1\"\n"
            << "\t\"name\"\t\t\""         << lua.gameName << "\"\n"
            << "\t\"StateFlags\"\t\t\""   << stateFlags   << "\"\n"
            << "\t\"installdir\"\t\t\""   << installDir   << "\"\n"
            << "\t\"lastupdated\"\t\t\"0\"\n"
            << "\t\"SizeOnDisk\"\t\t\""   << sizeOnDisk   << "\"\n"
            << "\t\"StagingSize\"\t\t\"0\"\n"
            << "\t\"buildid\"\t\t\"999999999\"\n"
            << installedDepots            << "\n"
            << platformConfig             << "\n"
            << "}\n";

        // Atomic write: write to .tmp then rename
        const std::string tmpPath = acfPath + ".tmp";
        {
            std::ofstream f(tmpPath, std::ios::trunc);
            if (!f.is_open())
            {
            PPLOG("cannot write ACF temp file %s\n", tmpPath.c_str());
                return false;
            }
            f << acf.str();
        }

        if (std::rename(tmpPath.c_str(), acfPath.c_str()) != 0)
        {
            PPLOG("rename ACF failed: %s\n", strerror(errno));
            std::remove(tmpPath.c_str());
            return false;
        }

        PPLOG("wrote ACF -> %s\n", acfPath.c_str());
        return true;
    }

    // ── Step 2: App token file ────────────────────────────────────────────────

    void writeAppToken(const Context& ctx)
    {
        const auto& lua = ctx.lua;
        if (lua.appToken.empty()) return;

        const std::string installDir = installFolderName(lua);
        const std::string gameDir =
            ctx.destPath + "/steamapps/common/" + installDir;
        const std::string tokenFile = gameDir + "/apptoken.txt";

        try { fs::create_directories(gameDir); }
        catch (...) {}

        std::ofstream f(tokenFile, std::ios::trunc);
        if (!f.is_open())
        {
            PPLOG("cannot write apptoken.txt at %s\n", tokenFile.c_str());
            return;
        }
        f << lua.appToken;
        PPLOG("wrote apptoken.txt -> %s\n", tokenFile.c_str());
    }

    // ── Step 3: Move manifests to depotcache ──────────────────────────────────

    void moveManifests(const Context& ctx)
    {
        const auto& lua = ctx.lua;

        if (ctx.manifestDir.empty() || ctx.depotcacheDir.empty()) return;

        try { fs::create_directories(ctx.depotcacheDir); }
        catch (...) {}

        int moved   = 0;
        int deleted = 0;

        for (const auto& [depotId, manifestGid] : lua.manifests)
        {
            const std::string filename = depotId + "_" + manifestGid + ".manifest";
            const std::string src      = ctx.manifestDir + "/" + filename;
            const std::string dst      = ctx.depotcacheDir + "/" + filename;

            if (!fs::exists(src)) continue;

            if (fs::file_size(src) == 0)
            {
                // Empty placeholder from API fallback — just delete it
                std::remove(src.c_str());
                ++deleted;
                continue;
            }

            // Move (rename first, copy+delete if cross-device)
            std::error_code ec;
            fs::rename(src, dst, ec);
            if (ec)
            {
                // Cross-device: copy then remove
                try
                {
                    fs::copy_file(src, dst,
                                  fs::copy_options::overwrite_existing);
                    std::remove(src.c_str());
                    ++moved;
                }
                catch (const std::exception& e)
                {
                    PPLOG("cannot move manifest %s: %s\n",
                             filename.c_str(), e.what());
                }
            }
            else
            {
                ++moved;
            }
        }

        // Remove the now-empty staging directory
        std::error_code ec;
        fs::remove(ctx.manifestDir, ec);

        PPLOG("manifests moved=%d deleted(placeholder)=%d\n", moved, deleted);
    }

    // ── Step 4: chmod ELF + script binaries ───────────────────────────────────

    void chmodBinaries(const Context& ctx)
    {
        if (ctx.dlResult.downloadDir.empty()) return;
        if (!fs::exists(ctx.dlResult.downloadDir)) return;

        const std::set<std::string> scriptExts = {
            ".sh", ".x86", ".x86_64", ".bin"
        };
        constexpr unsigned char elfMagic[4] = { 0x7f, 'E', 'L', 'F' };
        constexpr uintmax_t minElfSize = 1024;

        int count = 0;
        std::error_code ec;

        for (const auto& entry :
             fs::recursive_directory_iterator(ctx.dlResult.downloadDir,
                                              fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file(ec)) continue;

            const fs::path& p    = entry.path();
            const std::string fn = p.filename().string();
            std::string ext      = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool shouldChmod = false;

            if (scriptExts.count(ext))
            {
                shouldChmod = true;
            }
            else if (ext.empty())
            {
                // Extensionless file — check ELF magic
                const uintmax_t sz = fs::file_size(p, ec);
                if (!ec && sz >= minElfSize)
                {
                    unsigned char buf[4] = {};
                    FILE* fp = fopen(p.c_str(), "rb");
                    if (fp)
                    {
                        const bool gotMagic = fread(buf, 1, 4, fp) == 4;
                        fclose(fp);
                        if (gotMagic && memcmp(buf, elfMagic, 4) == 0)
                            shouldChmod = true;
                    }
                }
            }

            if (shouldChmod)
            {
                struct stat st{};
                if (stat(p.c_str(), &st) == 0)
                {
                    // Only touch files that don't already have any execute bit
                    if (!(st.st_mode & 0111))
                    {
                        if (chmod(p.c_str(), st.st_mode | 0755) == 0)
                            ++count;
                    }
                }
            }
        }

        if (count > 0)
            PPLOG("chmod +x applied to %d Linux binary/script(s)\n", count);
    }

    // ── Step 5: Update SLSsteam config.yaml ───────────────────────────────────

    void updateSLSConfig(const Context& ctx)
    {
        const auto& lua = ctx.lua;
        if (lua.appId.empty() || ctx.configPath.empty()) return;

        if (!fs::exists(ctx.configPath))
        {
            PPLOG("config.yaml not found at %s — skipping\n",
                  ctx.configPath.c_str());
            return;
        }

        // Read config line by line (same pattern as CConfig::addAdditionalAppId)
        std::vector<std::string> lines;
        {
            std::ifstream f(ctx.configPath);
            if (!f.is_open())
            {
                PPLOG("cannot read config.yaml: %s\n", ctx.configPath.c_str());
                return;
            }
            std::string line;
            while (std::getline(f, line))
                lines.push_back(line);
        }

        // ── AdditionalApps ────────────────────────────────────────────────────
        // Check if appId already present; if not, insert after last list entry
        bool addedApp = false;
        {
            bool inSection = false;
            bool alreadyExists = false;
            int  sectionHeaderIdx = -1;
            int  lastEntryIdx     = -1;

            for (int i = 0; i < static_cast<int>(lines.size()); ++i)
            {
                const auto& l = lines[i];
                if (!inSection)
                {
                    if (l.find("AdditionalApps:") == 0)
                    {
                        inSection = true;
                        sectionHeaderIdx = i;
                        continue;
                    }
                }
                else
                {
                    if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
                    {
                        inSection = false;
                        continue;
                    }
                    // Check for "- <appId>" (strip inline comment)
                    const size_t dash = l.find("- ");
                    if (dash != std::string::npos)
                    {
                        std::string val = l.substr(dash + 2);
                        const size_t cmt = val.find('#');
                        if (cmt != std::string::npos) val = val.substr(0, cmt);
                        while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
                            val.pop_back();
                        if (val == lua.appId) { alreadyExists = true; break; }
                        lastEntryIdx = i;
                    }
                }
            }

            if (!alreadyExists)
            {
                const std::string entry =
                    "  - " + lua.appId + "   # " + lua.gameName;
                if (sectionHeaderIdx == -1)
                {
                    lines.push_back("AdditionalApps:");
                    lines.push_back(entry);
                }
                else
                {
                    int after = (lastEntryIdx != -1) ? lastEntryIdx : sectionHeaderIdx;
                    lines.insert(lines.begin() + after + 1, entry);
                }
                addedApp = true;
            }
        }

        // ── AppTokens ─────────────────────────────────────────────────────────
        bool addedToken = false;
        if (!lua.appToken.empty())
        {
            const std::string tokenEntry = "  " + lua.appId + ": " + lua.appToken;
            const std::string prefix     = "  " + lua.appId + ":";

            bool inSection    = false;
            bool alreadyExists = false;
            int  sectionHeaderIdx = -1;
            int  lastEntryIdx     = -1;

            for (int i = 0; i < static_cast<int>(lines.size()); ++i)
            {
                const auto& l = lines[i];
                if (!inSection)
                {
                    if (l.find("AppTokens:") == 0)
                    {
                        inSection = true;
                        sectionHeaderIdx = i;
                        continue;
                    }
                }
                else
                {
                    if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
                    {
                        inSection = false;
                        continue;
                    }
                    if (l.rfind(prefix, 0) == 0) { alreadyExists = true; break; }
                    if (l.size() >= 2 && l[0] == ' ' && l[1] == ' ')
                        lastEntryIdx = i;
                }
            }

            if (!alreadyExists)
            {
                if (sectionHeaderIdx == -1)
                {
                    lines.push_back("AppTokens:");
                    lines.push_back(tokenEntry);
                }
                else
                {
                    int after = (lastEntryIdx != -1) ? lastEntryIdx : sectionHeaderIdx;
                    lines.insert(lines.begin() + after + 1, tokenEntry);
                }
                addedToken = true;
            }
        }

        if (!addedApp && !addedToken)
        {
            PPLOG("appId %s already present in config — nothing to write\n",
                  lua.appId.c_str());
            return;
        }

        // Atomic write
        const std::string tmpPath = ctx.configPath + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out.is_open())
            {
                PPLOG("cannot write config tmp file: %s\n", tmpPath.c_str());
                return;
            }
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out << lines[i];
                if (i + 1 < lines.size()) out << '\n';
            }
        }

        if (std::rename(tmpPath.c_str(), ctx.configPath.c_str()) != 0)
        {
            PPLOG("rename config failed: %s\n", strerror(errno));
            std::remove(tmpPath.c_str());
            return;
        }

        if (addedApp)
            PPLOG("added AppID %s to AdditionalApps in config.yaml\n",
                  lua.appId.c_str());
        if (addedToken)
            PPLOG("added AppToken for %s to config.yaml\n",
                  lua.appId.c_str());
    }

    // ── Main entry point ──────────────────────────────────────────────────────

    bool run(const Context& ctx)
    {
        PPLOG("starting post-processing for appid=%s\n",
                 ctx.lua.appId.c_str());

        const bool acfOk = writeAcf(ctx);
        if (!acfOk)
            PPLOG("ACF write failed — Steam may not see the game\n");

        moveManifests(ctx);
        chmodBinaries(ctx);
        updateSLSConfig(ctx);

        PPLOG("completed for appid=%s\n", ctx.lua.appId.c_str());
        return acfOk;
    }

} // namespace CppAccela::PostProcess
