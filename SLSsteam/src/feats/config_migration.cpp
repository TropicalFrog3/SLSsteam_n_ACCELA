#include "config_migration.hpp"
#include "../log.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>

bool ConfigMigration::backupConfig(const std::string& userConfigPath) {
    std::filesystem::path configPath(userConfigPath);
    if (!std::filesystem::exists(configPath)) {
        return false;
    }

    std::filesystem::path backupDir = configPath.parent_path() / "backups";
    std::error_code ec;
    if (!std::filesystem::exists(backupDir)) {
        std::filesystem::create_directories(backupDir, ec);
        if (ec) {
            LOG_NOTIFY("Failed to create backup directory: %s\n", ec.message().c_str());
            return false;
        }
    }

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &tm);
    
    std::filesystem::path backupPath = backupDir / ("config_" + std::string(timeStr) + ".yaml.bak");
    std::filesystem::copy_file(configPath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_NOTIFY("Failed to backup config: %s\n", ec.message().c_str());
        return false;
    }

    LOG_DEBUG("Config backed up to: %s\n", backupPath.string().c_str());
    return true;
}

void ConfigMigration::applyMigration(YAML::Node& userConfig, const YAML::Node& diffYaml) {
    if (!diffYaml["Migrations"]) {
        return;
    }

    uint32_t currentVersion = 0;
    if (userConfig["ConfigVersion"]) {
        currentVersion = userConfig["ConfigVersion"].as<uint32_t>();
    }

    const YAML::Node& migrations = diffYaml["Migrations"];
    uint32_t maxVersion = currentVersion;

    // YAML nodes map might not be sorted by key natively in iteration, but versions are integers.
    // Let's collect them and sort.
    std::map<uint32_t, YAML::Node> sortedMigrations;
    for (auto it = migrations.begin(); it != migrations.end(); ++it) {
        uint32_t version = it->first.as<uint32_t>();
        sortedMigrations[version] = it->second;
        if (version > maxVersion) {
            maxVersion = version;
        }
    }

    std::regex bridgeRegex(R"(\$\(([^)]+)\)\$\(([^)]*)\):)");

    for (const auto& [version, ops] : sortedMigrations) {
        if (version <= currentVersion) {
            continue; // Skip already applied migrations
        }

        LOG_NOTIFY("Applying config migration v%d\n", version);

        for (auto opIt = ops.begin(); opIt != ops.end(); ++opIt) {
            std::string keyPattern = opIt->first.as<std::string>();
            const YAML::Node& actionNode = opIt->second;
            std::string action = actionNode["action"].as<std::string>();

            std::smatch match;
            if (std::regex_match(keyPattern, match, bridgeRegex)) {
                std::string oldKey = match[1].str();
                std::string newKey = match[2].str(); // Can be empty if just removing/converting in place? No, syntax implies bridge.

                if (userConfig[oldKey]) {
                    if (action == "rename") {
                        if (!newKey.empty() && !userConfig[newKey]) {
                            userConfig[newKey] = userConfig[oldKey];
                        }
                        userConfig.remove(oldKey);
                    } else if (action == "rename_and_convert" || action == "convert") {
                        std::string oldValStr = userConfig[oldKey].as<std::string>();
                        bool mapped = false;

                        if (actionNode["value_mapping"]) {
                            const YAML::Node& mapping = actionNode["value_mapping"];
                            if (mapping[oldValStr]) {
                                if (!newKey.empty() && !userConfig[newKey]) {
                                    userConfig[newKey] = mapping[oldValStr];
                                }
                                mapped = true;
                            }
                        }

                        if (!mapped && actionNode["fallback_value"]) {
                            if (!newKey.empty() && !userConfig[newKey]) {
                                userConfig[newKey] = actionNode["fallback_value"];
                            }
                        } else if (!mapped && !actionNode["fallback_value"]) {
                            // If no mapping and no fallback, just copy the raw value if not removing
                            if (!newKey.empty() && !userConfig[newKey]) {
                                userConfig[newKey] = userConfig[oldKey];
                            }
                        }
                        
                        if (action == "rename_and_convert" || !newKey.empty()) {
                            userConfig.remove(oldKey);
                        }
                    } else if (action == "remove") {
                        userConfig.remove(oldKey);
                    }
                }
            } else {
                // Not a bridge syntax, maybe a simple remove or something on the old key
                std::string exactKey = keyPattern;
                if (userConfig[exactKey]) {
                    if (action == "remove") {
                        userConfig.remove(exactKey);
                    }
                }
            }
        }
        
        userConfig["ConfigVersion"] = version;
    }
}

void ConfigMigration::mergeTemplate(YAML::Node& userConfig, const YAML::Node& templateConfig) {
    if (!templateConfig.IsMap() || !userConfig.IsMap()) {
        return;
    }
    for (auto it = templateConfig.begin(); it != templateConfig.end(); ++it) {
        std::string key = it->first.as<std::string>();
        if (!userConfig[key]) {
            userConfig[key] = it->second;
        } else if (it->second.IsMap() && userConfig[key].IsMap()) {
            YAML::Node subUserConfig = userConfig[key];
            mergeTemplate(subUserConfig, it->second);
        }
    }
}

bool ConfigMigration::migrate(const std::string& userConfigPath, const std::string& diffYamlPath, const std::string& templatePath) {
    if (!std::filesystem::exists(userConfigPath) || !std::filesystem::exists(diffYamlPath)) {
        return false;
    }

    try {
        YAML::Node userConfig = YAML::LoadFile(userConfigPath);
        YAML::Node diffYaml = YAML::LoadFile(diffYamlPath);
        
        YAML::Node templateConfig;
        if (std::filesystem::exists(templatePath)) {
            templateConfig = YAML::LoadFile(templatePath);
        }

        uint32_t originalVersion = 0;
        if (userConfig["ConfigVersion"]) {
            originalVersion = userConfig["ConfigVersion"].as<uint32_t>();
        }

        backupConfig(userConfigPath);
        applyMigration(userConfig, diffYaml);
        if (templateConfig.IsDefined()) {
            mergeTemplate(userConfig, templateConfig);
        }

        uint32_t newVersion = 0;
        if (userConfig["ConfigVersion"]) {
            newVersion = userConfig["ConfigVersion"].as<uint32_t>();
        }

        // Always write if template introduced new keys or migration applied. 
        // For simplicity, we just dump it out. To be atomic, write to a temp file first.
        std::string tmpPath = userConfigPath + ".tmp";
        std::ofstream fout(tmpPath);
        fout << userConfig;
        fout.close();

        // fsync / flush should be handled by standard library before close, but we can do a rename which is atomic in POSIX.
        std::error_code ec;
        std::filesystem::rename(tmpPath, userConfigPath, ec);
        if (ec) {
            LOG_NOTIFY("Atomic rename failed for config migration: %s\n", ec.message().c_str());
            return false;
        }
        
        return true;
    } catch (const YAML::Exception& e) {
        LOG_NOTIFY("YAML Exception during config migration: %s\n", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_NOTIFY("Exception during config migration: %s\n", e.what());
        return false;
    }
}
