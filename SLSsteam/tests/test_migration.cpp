#include "../src/feats/config_migration.hpp"
#include "../src/feats/cache_migration.hpp"
#include "../src/log.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>

// Provide minimal CLog stub so test_migration does not depend on log.cpp or g_config
void CLog::trace(const char*, const char*, const int, const char*, ...) {}
void CLog::traceOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::once(const char*, const char*, const int, const char*, ...) {}
void CLog::debug(const char*, const char*, const int, const char*, ...) {}
void CLog::debugOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::warn(const char*, const char*, const int, const char*, ...) {}
void CLog::error(const char*, const char*, const int, const char*, ...) {}
void CLog::info(const char*, const char*, const int, const char*, ...) {}
void CLog::notify(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyLong(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyWarn(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyError(const char*, const char*, const int, const char*, ...) {}
void CLog::api(const char*, const char*, const int, const char*, ...) {}
void CLog::custom(const unsigned int, const char*, const char*, const int, const char*, ...) {}

CLog::CLog(const char* p) : path(p ? p : "") {}
CLog::~CLog() {}
CLog* CLog::createDefaultLog() { return new CLog("/dev/null"); }

std::unique_ptr<CLog> g_pLog = std::make_unique<CLog>("/dev/null");

void testConfigMigration()
{
    std::cout << "[TEST] Running ConfigMigration unit tests...\n" << std::flush;
    std::string testDir = "/tmp/sls_test_config_mig";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    std::string userConfig = testDir + "/config.yaml";
    std::string diffYaml = testDir + "/config.diff.yaml";
    std::string templatePath = testDir + "/template.yaml";

    // Write initial user config (v0)
    {
        std::ofstream f(userConfig);
        f << "LogLevels: \"0xff\"\n";
        f << "RyuuCookies: \"legacy_secret\"\n";
        f << "UserAppIds:\n  - 10\n  - 20\n";
    }

    // Write template config
    {
        std::ofstream f(templatePath);
        f << "DefaultNewSetting: true\n";
        f << "LogLevelsBasic: 1\n";
    }

    // Write diff yaml
    {
        std::ofstream f(diffYaml);
        f << "Migrations:\n";
        f << "  1:\n";
        f << "    \"$(LogLevels)$(LogLevelsBasic):\":\n";
        f << "      action: rename_and_convert\n";
        f << "      value_mapping:\n";
        f << "        \"0xff\": 5\n";
        f << "    \"$(RyuuCookies)$():\":\n";
        f << "      action: remove\n";
    }

    std::cout << "[TEST] Calling ConfigMigration::migrate...\n" << std::flush;
    bool res = ConfigMigration::migrate(userConfig, diffYaml, templatePath);
    std::cout << "[TEST] Migrate returned: " << res << "\n" << std::flush;
    assert(res && "ConfigMigration::migrate should return true");

    // Verify migrated config
    YAML::Node migrated = YAML::LoadFile(userConfig);
    std::cout << "[TEST] Loaded migrated config\n" << std::flush;
    assert(!migrated["LogLevels"] && "LogLevels should have been removed");
    std::cout << "[TEST] LogLevels check passed\n" << std::flush;
    assert(migrated["LogLevelsBasic"] && "LogLevelsBasic should be present");
    std::cout << "[TEST] LogLevelsBasic check passed\n" << std::flush;
    assert(migrated["LogLevelsBasic"].as<int>() == 5 && "LogLevelsBasic should be converted to 5");
    std::cout << "[TEST] Value check passed\n" << std::flush;
    assert(!migrated["RyuuCookies"] && "RyuuCookies should have been removed");
    assert(migrated["UserAppIds"] && "UserAppIds should be preserved");
    assert(migrated["DefaultNewSetting"] && "DefaultNewSetting from template should be merged");
    assert(migrated["ConfigVersion"].as<int>() == 1 && "ConfigVersion should be updated to 1");
    std::cout << "[TEST] Config version check passed\n" << std::flush;

    // Verify backup exists
    std::string backupDir = testDir + "/backups";
    assert(std::filesystem::exists(backupDir) && "Backup directory should exist");
    bool foundBak = false;
    for (const auto& e : std::filesystem::directory_iterator(backupDir))
    {
        if (e.path().extension() == ".bak") foundBak = true;
    }
    assert(foundBak && "Timestamped backup file should exist");

    std::filesystem::remove_all(testDir);
    std::cout << "[TEST] ConfigMigration tests passed successfully!\n";
}

void testCacheMigration()
{
    std::cout << "[TEST] Running CacheMigration unit tests...\n";
    std::string testDir = "/tmp/sls_test_cache_mig";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    // 1. Generic cache directory for testing migration
    std::string oldCacheDir = testDir + "/legacy_depots";
    std::filesystem::create_directories(oldCacheDir);
    {
        std::ofstream f(oldCacheDir + "/123.dat");
        f << "dummy cache content\n";
    }

    // 2. Manifest, Lua, and Steam depotcache directories that must be STRICTLY preserved without any modification
    std::string manifestDir = testDir + "/morrenus_manifests";
    std::filesystem::create_directories(manifestDir);
    {
        std::ofstream f(manifestDir + "/123.manifest");
        f << "untouchable manifest content\n";
    }
    std::string luaDir = testDir + "/stplug-in";
    std::filesystem::create_directories(luaDir);
    {
        std::ofstream f(luaDir + "/test.lua");
        f << "untouchable lua script\n";
    }
    std::string depotcacheDir = testDir + "/depotcache";
    std::filesystem::create_directories(depotcacheDir);
    {
        std::ofstream f(depotcacheDir + "/456_789.manifest");
        f << "untouchable steam depotcache manifest\n";
    }

    std::string diffYaml = testDir + "/cache.diff.yaml";
    std::string verFile = testDir + "/cache_version";

    {
        std::ofstream f(diffYaml);
        f << "CacheVersion: 1\n";
        f << "Migrations:\n";
        f << "  1:\n";
        // Valid migration for generic cache
        f << "    \"$(legacy_depots)$(depots_v2):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(*.dat)$(*.dat.cache):\":\n";
        f << "      action: rename_files\n";
        f << "      target_dir: \"" << testDir << "/depots_v2\"\n";
        // Attempted forbidden migrations on manifest, lua, and depotcache (must be skipped by safeguard)
        f << "    \"$(morrenus_manifests)$(tampered_manifests):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(stplug-in)$(tampered_luas):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(depotcache)$(tampered_depotcache):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
    }

    bool res = CacheMigration::migrate(diffYaml, verFile);
    assert(res && "CacheMigration::migrate should return true");

    // Verify generic directory was renamed
    assert(!std::filesystem::exists(oldCacheDir) && "Old generic cache dir should be migrated");
    std::string newCacheDir = testDir + "/depots_v2";
    assert(std::filesystem::exists(newCacheDir) && "New generic cache dir should exist");

    // Verify file inside was renamed
    assert(std::filesystem::exists(newCacheDir + "/123.dat.cache") && "File should be renamed to .dat.cache");

    // Verify that manifests, luas, and depotcache directories were STRICTLY PRESERVED and untouched
    assert(std::filesystem::exists(manifestDir + "/123.manifest") && "Manifest directory and files MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_manifests") && "Manifest migration MUST NOT occur");
    assert(std::filesystem::exists(luaDir + "/test.lua") && "Lua directory and files MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_luas") && "Lua migration MUST NOT occur");
    assert(std::filesystem::exists(depotcacheDir + "/456_789.manifest") && "Steam depotcache MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_depotcache") && "Depotcache migration MUST NOT occur");

    // Verify version file
    std::ifstream vf(verFile);
    int v = 0;
    vf >> v;
    assert(v == 1 && "CacheVersion should be 1");

    std::filesystem::remove_all(testDir);
    std::cout << "[TEST] CacheMigration tests passed successfully with manifest/lua preservation!\n";
}

void testMalformedConfigMigration()
{
    std::cout << "[TEST] Running Malformed Config test...\n";
    std::string testDir = "/tmp/sls_test_malformed";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    std::string userConfig = testDir + "/config.yaml";
    std::string diffYaml = testDir + "/config.diff.yaml";

    {
        std::ofstream f(userConfig);
        f << "LogLevels: [unclosed list\nInvalid: {syntax\n";
    }
    {
        std::ofstream f(diffYaml);
        f << "Migrations:\n  1:\n    \"$(LogLevels)$(LogLevelsBasic):\":\n      action: rename\n";
    }

    bool res = ConfigMigration::migrate(userConfig, diffYaml);
    assert(!res && "ConfigMigration should fail cleanly on malformed YAML");
    std::cout << "[TEST] Malformed config safely rejected!\n";

    std::filesystem::remove_all(testDir);
}

void testMissingFiles()
{
    std::cout << "[TEST] Running Missing Files test...\n";
    bool res = ConfigMigration::migrate("/nonexistent/path/config.yaml", "/nonexistent/diff.yaml");
    assert(!res && "ConfigMigration should return false for nonexistent files");
    
    bool cRes = CacheMigration::migrate("/nonexistent/cache.diff.yaml", "/nonexistent/cache_ver");
    assert(!cRes && "CacheMigration should return false for nonexistent files");
    std::cout << "[TEST] Missing files safely handled!\n";
}

int main()
{
    testConfigMigration();
    testCacheMigration();
    testMalformedConfigMigration();
    testMissingFiles();
    std::cout << "[ALL UNIT TESTS PASSED]\n";
    return 0;
}
