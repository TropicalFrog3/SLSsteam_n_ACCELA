#pragma once

#include <string>
#include <vector>

class CacheMigration {
public:
    // Performs cache backup and migrations defined in diffYamlPath.
    // diffYamlPath: path to cache.diff.yaml
    // cacheVersionPath: path to file storing current cache version integer
    static bool migrate(const std::string& diffYamlPath, const std::string& cacheVersionPath);

    // Explicitly triggers a backup of known cache directories
    static bool backupCaches(const std::vector<std::string>& cacheDirs, const std::string& backupDestDir);

    // Resolves '~' or relative paths to absolute paths
    static std::string resolvePath(const std::string& path);
};
