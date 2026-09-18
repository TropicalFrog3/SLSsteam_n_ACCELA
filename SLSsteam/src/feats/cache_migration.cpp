#include "cache_migration.hpp"
#include "../log.hpp"

#include <yaml-cpp/yaml.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

std::string CacheMigration::resolvePath(const std::string& path)
{
    if (path.empty()) return path;
    if (path[0] == '~')
    {
        const char* home = std::getenv("HOME");
        if (home)
        {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

bool CacheMigration::backupCaches(const std::vector<std::string>& cacheDirs, const std::string& backupDestDir)
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&t);

    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &tm);

    std::filesystem::path backupRoot = std::filesystem::path(resolvePath(backupDestDir)) / ("cache_" + std::string(timeStr));
    std::error_code ec;

    bool anyBackedUp = false;
    for (const auto& rawDir : cacheDirs)
    {
        std::filesystem::path dirPath = resolvePath(rawDir);
        if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath))
        {
            std::filesystem::path target = backupRoot / dirPath.filename();
            std::filesystem::create_directories(target, ec);
            std::filesystem::copy(dirPath, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                anyBackedUp = true;
                LOG_DEBUG("CacheMigration: Backed up %s -> %s\n", dirPath.string().c_str(), target.string().c_str());
            }
        }
    }

    if (anyBackedUp)
    {
        LOG_INFO("CacheMigration: Cache backup created at %s\n", backupRoot.string().c_str());
    }
    return true;
}

bool CacheMigration::migrate(const std::string& diffYamlPath, const std::string& cacheVersionPath)
{
    std::string resolvedDiffPath = resolvePath(diffYamlPath);
    if (!std::filesystem::exists(resolvedDiffPath))
    {
        return false;
    }

    try
    {
        YAML::Node diffYaml = YAML::LoadFile(resolvedDiffPath);
        if (!diffYaml["Migrations"] || !diffYaml["Migrations"].IsMap())
        {
            return true;
        }

        std::string resolvedVerPath = resolvePath(cacheVersionPath);
        uint32_t currentVersion = 0;
        if (std::filesystem::exists(resolvedVerPath))
        {
            std::ifstream vf(resolvedVerPath);
            if (vf.is_open())
            {
                vf >> currentVersion;
                vf.close();
            }
        }

        std::map<uint32_t, YAML::Node> sortedMigrations;
        uint32_t maxVersion = currentVersion;

        for (auto it = diffYaml["Migrations"].begin(); it != diffYaml["Migrations"].end(); ++it)
        {
            uint32_t ver = it->first.as<uint32_t>();
            sortedMigrations[ver] = it->second;
            if (ver > maxVersion)
            {
                maxVersion = ver;
            }
        }

        if (sortedMigrations.empty() || maxVersion <= currentVersion)
        {
            return true;
        }

        // Automatic pre-migration backup of standard cache directories
        const char* home = std::getenv("HOME");
        std::string backupBase = home ? (std::string(home) + "/.local/share/SLSsteam/cache_backups") : "/tmp";
        std::vector<std::string> defaultCaches = {
            "~/.local/share/ACCELA/depots",
            "~/.local/share/SLSsteam/cache"
        };
        backupCaches(defaultCaches, backupBase);

        std::regex bridgeRegex(R"(\$\(([^)]*)\)\$\(([^)]*)\):?)");

        for (const auto& [ver, ops] : sortedMigrations)
        {
            if (ver <= currentVersion) continue;

            LOG_INFO("CacheMigration: Applying cache migration v%u\n", ver);

            for (auto opIt = ops.begin(); opIt != ops.end(); ++opIt)
            {
                std::string keyPattern = opIt->first.as<std::string>();
                const YAML::Node& actionNode = opIt->second;
                std::string action = actionNode["action"] ? actionNode["action"].as<std::string>() : "rename_dir";

                std::string oldKey;
                std::string newKey;

                std::smatch match;
                if (std::regex_match(keyPattern, match, bridgeRegex))
                {
                    oldKey = match[1].str();
                    newKey = match[2].str();
                }
                else
                {
                    oldKey = keyPattern;
                    newKey = "";
                }

                // Explicit safeguard: Never modify, rename, or diff luas, manifest directories, or depotcache
                if (oldKey.find("manifest") != std::string::npos || oldKey.find("lua") != std::string::npos || oldKey.find("depotcache") != std::string::npos ||
                    newKey.find("manifest") != std::string::npos || newKey.find("lua") != std::string::npos || newKey.find("depotcache") != std::string::npos)
                {
                    LOG_INFO("CacheMigration: Skipping rule for '%s' -> '%s' (luas, manifests, and depotcache are strictly preserved as is)\n",
                             oldKey.c_str(), newKey.c_str());
                    continue;
                }

                std::string parentPath = actionNode["parent_path"] ? resolvePath(actionNode["parent_path"].as<std::string>()) : "";
                std::string targetDir = actionNode["target_dir"] ? resolvePath(actionNode["target_dir"].as<std::string>()) : "";

                std::error_code ec;

                if (action == "rename_dir" || action == "move_dir")
                {
                    std::filesystem::path src = parentPath.empty() ? std::filesystem::path(resolvePath(oldKey)) : (std::filesystem::path(parentPath) / oldKey);
                    std::filesystem::path dst = parentPath.empty() ? std::filesystem::path(resolvePath(newKey)) : (std::filesystem::path(parentPath) / newKey);

                    if (std::filesystem::exists(src))
                    {
                        if (std::filesystem::exists(dst))
                        {
                            // Merge contents into dst
                            for (const auto& entry : std::filesystem::directory_iterator(src))
                            {
                                std::filesystem::rename(entry.path(), dst / entry.path().filename(), ec);
                            }
                            std::filesystem::remove_all(src, ec);
                        }
                        else
                        {
                            std::filesystem::create_directories(dst.parent_path(), ec);
                            std::filesystem::rename(src, dst, ec);
                        }
                        LOG_INFO("CacheMigration: Renamed cache dir %s -> %s\n", src.string().c_str(), dst.string().c_str());
                    }
                }
                else if (action == "remove_dir")
                {
                    std::filesystem::path src = parentPath.empty() ? std::filesystem::path(resolvePath(oldKey)) : (std::filesystem::path(parentPath) / oldKey);
                    if (std::filesystem::exists(src))
                    {
                        std::filesystem::remove_all(src, ec);
                        LOG_INFO("CacheMigration: Removed obsolete cache dir %s\n", src.string().c_str());
                    }
                }
                else if (action == "rename_files")
                {
                    std::filesystem::path dir = targetDir.empty() ? std::filesystem::path(resolvePath(parentPath)) : std::filesystem::path(targetDir);
                    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir))
                    {
                        // Match extension pattern like *.manifest -> *.manifest.cache
                        std::string oldExt = oldKey;
                        std::string newExt = newKey;
                        if (oldExt.rfind("*.", 0) == 0) oldExt = oldExt.substr(1);
                        if (newExt.rfind("*.", 0) == 0) newExt = newExt.substr(1);

                        for (const auto& entry : std::filesystem::directory_iterator(dir))
                        {
                            if (!entry.is_regular_file()) continue;
                            std::string filename = entry.path().filename().string();
                            if (filename.ends_with(oldExt))
                            {
                                std::string baseName = filename.substr(0, filename.size() - oldExt.size());
                                std::filesystem::path newFilePath = entry.path().parent_path() / (baseName + newExt);
                                std::filesystem::rename(entry.path(), newFilePath, ec);
                                LOG_DEBUG("CacheMigration: Renamed file %s -> %s\n", filename.c_str(), newFilePath.filename().string().c_str());
                            }
                        }
                    }
                }
            }
        }

        // Atomically write updated cache version
        std::error_code verEc;
        std::filesystem::create_directories(std::filesystem::path(resolvedVerPath).parent_path(), verEc);
        std::string tmpVerPath = resolvedVerPath + ".tmp";
        std::ofstream vf(tmpVerPath);
        if (vf.is_open())
        {
            vf << maxVersion << "\n";
            vf.close();
            std::filesystem::rename(tmpVerPath, resolvedVerPath, verEc);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("CacheMigration: Exception during cache migration: %s\n", e.what());
        return false;
    }
}
