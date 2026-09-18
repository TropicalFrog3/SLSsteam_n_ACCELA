#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

class ConfigMigration {
public:
    static bool migrate(const std::string& userConfigPath, const std::string& diffYamlPath, const std::string& templatePath = "");

private:
    static bool backupConfig(const std::string& userConfigPath);
    static void applyMigration(YAML::Node& userConfig, const YAML::Node& diffYaml);
    static void mergeTemplate(YAML::Node& userConfig, const YAML::Node& templateConfig);
};
