#include "autoupdate.hpp"
#include "../curl.hpp"
#include "../log.hpp"
#include "../version.hpp"

#include <yaml-cpp/yaml.h>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace AutoUpdate
{
    static const char* REMOTE_VERSION_URL = "https://raw.githubusercontent.com/TropicalFrog3/SLSsteam_n_ACCELA/refs/heads/main/SLSsteam/res/version";

    static std::string getRemoteVersionUrl()
    {
        const char* envUrl = std::getenv("SLS_UPDATE_VERSION_URL");
        if (envUrl && envUrl[0] != '\0')
        {
            return std::string(envUrl);
        }
        return REMOTE_VERSION_URL;
    }

    static size_t curlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* file = static_cast<FILE*>(userdata);
        return fwrite(ptr, size, nmemb, file);
    }

    static void sendNotification(const std::string& title, const std::string& msg, const std::string& urgency = "normal")
    {
        const char* testEnv = std::getenv("SLS_TEST_ENV");
        if (testEnv && (std::string(testEnv) == "1" || std::string(testEnv) == "true"))
        {
            return;
        }
        std::string cmd = "notify-send -u " + urgency + " \"" + title + "\" \"" + msg + "\" 2>/dev/null";
        system(cmd.c_str());
    }

    static bool downloadToFile(const std::string& url, const std::string& destPath)
    {
        // Support local file paths and file:// scheme for testing and offline faking
        std::string localFilePath;
        if (url.rfind("file://", 0) == 0)
        {
            localFilePath = url.substr(7);
        }
        else if (url.rfind("/", 0) == 0)
        {
            localFilePath = url;
        }

        if (!localFilePath.empty())
        {
            std::error_code ec;
            std::filesystem::copy_file(localFilePath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                LOG_INFO("AutoUpdate: Copied mock release from %s -> %s\n", localFilePath.c_str(), destPath.c_str());
                return true;
            }
            LOG_WARN("AutoUpdate: Failed to copy local mock release: %s\n", ec.message().c_str());
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) return false;

        FILE* fp = fopen(destPath.c_str(), "wb");
        if (!fp)
        {
            curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (res != CURLE_OK)
        {
            std::filesystem::remove(destPath);
            curl_easy_cleanup(curl);
            return false;
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        return httpCode == 200;
    }

    static std::vector<int> parseVersionNumbers(const std::string& versionStr)
    {
        std::vector<int> parts;
        std::string current;
        for (char c : versionStr)
        {
            if (std::isdigit(c))
            {
                current += c;
            }
            else
            {
                if (!current.empty())
                {
                    parts.push_back(std::stoi(current));
                    current.clear();
                }
            }
        }
        if (!current.empty())
        {
            parts.push_back(std::stoi(current));
        }
        return parts;
    }

    static bool isRemoteNewer(const std::string& local, const std::string& remote)
    {
        auto localParts = parseVersionNumbers(local);
        auto remoteParts = parseVersionNumbers(remote);
        
        size_t size = std::max(localParts.size(), remoteParts.size());
        for (size_t i = 0; i < size; ++i)
        {
            int localVal = (i < localParts.size()) ? localParts[i] : 0;
            int remoteVal = (i < remoteParts.size()) ? remoteParts[i] : 0;
            
            if (remoteVal > localVal) return true;
            if (remoteVal < localVal) return false;
        }
        return false;
    }

    static std::string calculateSHA256(const std::string& filePath)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        if (!SHA256_Init(&sha256)) return "";

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return "";

        char buffer[32768];
        while (file.read(buffer, sizeof(buffer)))
        {
            SHA256_Update(&sha256, buffer, file.gcount());
        }
        if (file.gcount() > 0)
        {
            SHA256_Update(&sha256, buffer, file.gcount());
        }

        SHA256_Final(hash, &sha256);

        std::ostringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    static bool validateManifest(const std::string& releaseRoot)
    {
        std::string manifestPath = releaseRoot + "/update-manifest.yaml";
        if (!std::filesystem::exists(manifestPath))
        {
            LOG_INFO("AutoUpdate: No update-manifest.yaml found in release, skipping checksum validation.\n");
            return true;
        }

        try
        {
            YAML::Node manifest = YAML::LoadFile(manifestPath);
            if (!manifest["files"] || !manifest["files"].IsMap())
            {
                LOG_WARN("AutoUpdate: update-manifest.yaml has invalid format\n");
                return false;
            }

            for (auto it = manifest["files"].begin(); it != manifest["files"].end(); ++it)
            {
                std::string relPath = it->first.as<std::string>();
                std::string expectedHash = it->second.as<std::string>();

                std::filesystem::path fullPath = std::filesystem::path(releaseRoot) / relPath;
                if (!std::filesystem::exists(fullPath))
                {
                    LOG_WARN("AutoUpdate: Manifest validation failed: missing file %s\n", relPath.c_str());
                    return false;
                }

                std::string actualHash = calculateSHA256(fullPath.string());
                if (actualHash != expectedHash)
                {
                    LOG_WARN("AutoUpdate: Manifest checksum mismatch for %s (expected: %s, actual: %s)\n",
                             relPath.c_str(), expectedHash.c_str(), actualHash.c_str());
                    return false;
                }
            }

            LOG_INFO("AutoUpdate: Payload integrity verified successfully with update-manifest.yaml\n");
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_WARN("AutoUpdate: Error reading update-manifest.yaml: %s\n", e.what());
            return false;
        }
    }

    static void doCheckAndPrompt()
    {
        LOG_INFO("AutoUpdate: Checking for updates...\n");

        std::string versionUrl = getRemoteVersionUrl();
        std::string data;

        if (versionUrl.rfind("file://", 0) == 0 || versionUrl.rfind("/", 0) == 0)
        {
            std::string localPath = (versionUrl.rfind("file://", 0) == 0) ? versionUrl.substr(7) : versionUrl;
            std::ifstream vf(localPath);
            if (!vf.is_open())
            {
                LOG_DEBUG("AutoUpdate: Failed to open local version manifest %s\n", localPath.c_str());
                return;
            }
            std::ostringstream ss;
            ss << vf.rdbuf();
            data = ss.str();
            LOG_INFO("AutoUpdate: Read mock version manifest from %s\n", localPath.c_str());
        }
        else
        {
            int res = Curl::getString(versionUrl.c_str(), data);
            if (res != 0)
            {
                LOG_DEBUG("AutoUpdate: Failed to fetch remote version (curl code %d)\n", res);
                return;
            }
        }

        std::string remoteVersionStr;
        std::string changelog;
        std::string downloadUrl;

        try
        {
            YAML::Node node = YAML::Load(data);
            if (node["Version"])
            {
                remoteVersionStr = node["Version"].as<std::string>();
            }
            if (node["Changelog"])
            {
                changelog = node["Changelog"].as<std::string>();
            }
            if (node["DownloadUrl"])
            {
                downloadUrl = node["DownloadUrl"].as<std::string>();
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN("AutoUpdate: Failed to parse remote version manifest: %s\n", e.what());
            return;
        }

        if (!isRemoteNewer(VERSION, remoteVersionStr))
        {
            LOG_INFO("AutoUpdate: Already up to date (Local: %s, Remote: %s)\n", VERSION, remoteVersionStr.c_str());
            return;
        }

        LOG_INFO("AutoUpdate: Newer version available (Local: %s, Remote: %s)\n", VERSION, remoteVersionStr.c_str());

        // Replace version placeholder in DownloadUrl
        size_t pos = downloadUrl.find("${version}");
        if (pos != std::string::npos)
        {
            downloadUrl.replace(pos, 10, remoteVersionStr);
        }

        // Prepare prompt text
        std::string text = std::string(VERSION) + " -> " + remoteVersionStr + 
                           " newer update is available, would you like to update now?\n\nDetails of the update:\n" + changelog;

        const char* autoAcceptEnv = std::getenv("SLS_UPDATE_AUTO_ACCEPT");
        bool autoAccept = (autoAcceptEnv && (std::string(autoAcceptEnv) == "1" || std::string(autoAcceptEnv) == "true"));

        if (!autoAccept)
        {
            // Fork to launch zenity dialog safely
            pid_t pid = fork();
            if (pid < 0)
            {
                LOG_WARN("AutoUpdate: fork() failed for update prompt\n");
                return;
            }

            if (pid == 0)
            {
                // Child process: launch zenity
                execlp("zenity", "zenity", "--question", "--title=SLSsteam Update", 
                       "--text", text.c_str(), "--ok-label=Install", "--cancel-label=Close", 
                       "--width=550", "--height=350", nullptr);
                _exit(127); // If zenity is missing
            }

            int status = 0;
            if (waitpid(pid, &status, 0) < 0)
            {
                LOG_WARN("AutoUpdate: waitpid() failed on prompt\n");
                return;
            }

            if (!WIFEXITED(status))
            {
                return;
            }

            int exitStatus = WEXITSTATUS(status);
            if (exitStatus == 127)
            {
                LOG_INFO("AutoUpdate: zenity not found. Falling back to notification alert.\n");
                sendNotification("SLSsteam", "Update available! Newer version is ready. Run setup.sh to update.");
                return;
            }

            if (exitStatus != 0)
            {
                // User cancelled/closed the dialog
                LOG_INFO("AutoUpdate: User declined the update.\n");
                return;
            }
        }
        else
        {
            LOG_INFO("AutoUpdate: SLS_UPDATE_AUTO_ACCEPT is enabled, auto-accepting update.\n");
        }

        // User clicked "Install"
        LOG_INFO("AutoUpdate: User accepted update. Starting download...\n");
        sendNotification("SLSsteam", "Downloading update...");

        // Determine destination folder
        const char* home = getenv("HOME");
        if (!home)
        {
            LOG_WARN("AutoUpdate: HOME environment variable not set, aborting update.\n");
            sendNotification("SLSsteam", "Update failed: HOME environment variable not found.", "critical");
            return;
        }

        bool isFlatpak = false;
        std::string installDir = std::string(home) + "/.local/share/SLSsteam";
        if (std::filesystem::exists(std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam/SLSsteam.so"))
        {
            installDir = std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam";
            isFlatpak = true;
        }

        // Setup secure, isolated staging directory in user directory
        std::string updatesBaseDir = installDir + "/updates";
        auto now = std::chrono::system_clock::now().time_since_epoch();
        long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        std::string stagingDir = updatesBaseDir + "/staging_" + std::to_string(timestamp) + "_" + std::to_string(getpid());

        std::error_code ec;
        std::filesystem::create_directories(stagingDir, ec);
        if (ec)
        {
            LOG_WARN("AutoUpdate: Failed to create staging directory %s: %s\n", stagingDir.c_str(), ec.message().c_str());
            sendNotification("SLSsteam", "Update failed: Could not create staging directory.", "critical");
            return;
        }
        std::filesystem::permissions(stagingDir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

        std::string ext = ".zip";
        if (downloadUrl.find(".7z") != std::string::npos)
        {
            ext = ".7z";
        }

        std::string archivePath = stagingDir + "/release" + ext;
        std::string extractDir = stagingDir + "/extracted";

        if (!downloadToFile(downloadUrl, archivePath))
        {
            LOG_WARN("AutoUpdate: Failed to download update from %s\n", downloadUrl.c_str());
            sendNotification("SLSsteam", "Update failed: Download failed.", "critical");
            std::filesystem::remove_all(stagingDir, ec);
            return;
        }

        std::filesystem::create_directories(extractDir, ec);

        bool extracted = false;
        if (ext == ".zip")
        {
            pid_t extPid = fork();
            if (extPid == 0)
            {
                execlp("unzip", "unzip", "-o", "-q", archivePath.c_str(), "-d", extractDir.c_str(), nullptr);
                _exit(127);
            }
            int extStatus = 0;
            waitpid(extPid, &extStatus, 0);
            if (WIFEXITED(extStatus) && WEXITSTATUS(extStatus) == 0)
            {
                extracted = true;
            }
            else
            {
                // Fallback to 7z
                extPid = fork();
                if (extPid == 0)
                {
                    std::string outOpt = "-o" + extractDir;
                    execlp("7z", "7z", "x", "-y", outOpt.c_str(), archivePath.c_str(), nullptr);
                    _exit(127);
                }
                waitpid(extPid, &extStatus, 0);
                if (WIFEXITED(extStatus) && WEXITSTATUS(extStatus) == 0)
                {
                    extracted = true;
                }
            }
        }
        else if (ext == ".7z")
        {
            pid_t extPid = fork();
            if (extPid == 0)
            {
                std::string outOpt = "-o" + extractDir;
                execlp("7z", "7z", "x", "-y", outOpt.c_str(), archivePath.c_str(), nullptr);
                _exit(127);
            }
            int extStatus = 0;
            waitpid(extPid, &extStatus, 0);
            if (WIFEXITED(extStatus) && WEXITSTATUS(extStatus) == 0)
            {
                extracted = true;
            }
        }

        if (!extracted)
        {
            LOG_WARN("AutoUpdate: Extraction failed for %s\n", archivePath.c_str());
            sendNotification("SLSsteam", "Update failed: Extraction tools not found or failed.", "critical");
            std::filesystem::remove_all(stagingDir, ec);
            return;
        }

        // Locate release root directory (can be extractDir or nested subdirectory)
        std::string releaseRoot = extractDir;
        if (!std::filesystem::exists(releaseRoot + "/install.sh"))
        {
            for (const auto& entry : std::filesystem::directory_iterator(extractDir))
            {
                if (entry.is_directory() && std::filesystem::exists(entry.path() / "install.sh"))
                {
                    releaseRoot = entry.path().string();
                    break;
                }
            }
        }

        // Validate archive structure
        if (!std::filesystem::exists(releaseRoot + "/install.sh"))
        {
            LOG_WARN("AutoUpdate: install.sh missing in extracted archive at %s\n", releaseRoot.c_str());
            sendNotification("SLSsteam", "Update failed: Corrupted archive, install.sh missing.", "critical");
            std::filesystem::remove_all(stagingDir, ec);
            return;
        }

        // Validate payload integrity against update-manifest.yaml if present
        if (!validateManifest(releaseRoot))
        {
            LOG_WARN("AutoUpdate: Integrity verification failed for %s\n", releaseRoot.c_str());
            sendNotification("SLSsteam", "Update failed: Payload integrity check failed.", "critical");
            std::filesystem::remove_all(stagingDir, ec);
            return;
        }

        LOG_INFO("AutoUpdate: Preparing transactional update script...\n");
        sendNotification("SLSsteam", "Update verified! Applying update, please wait...");

        std::string scriptPath = stagingDir + "/updater.sh";
        {
            std::ofstream script(scriptPath);
            if (!script.is_open())
            {
                LOG_WARN("AutoUpdate: Failed to create updater script at %s\n", scriptPath.c_str());
                sendNotification("SLSsteam", "Update failed: Could not create updater script.", "critical");
                std::filesystem::remove_all(stagingDir, ec);
                return;
            }

            script << "#!/bin/bash\n";
            script << "set -u\n\n";
            script << "log() {\n";
            script << "    echo \"[SLS_UPDATER] $1\"\n";
            script << "}\n\n";

            // 1. Graceful Steam termination with fallback to SIGTERM and SIGKILL
            script << "if [ \"${SLS_TEST_ENV:-0}\" != \"1\" ]; then\n";
            script << "    log \"Requesting Steam shutdown...\"\n";
            if (isFlatpak)
            {
                script << "    if command -v flatpak >/dev/null 2>&1; then\n";
                script << "        flatpak kill com.valvesoftware.Steam 2>/dev/null || true\n";
                script << "    fi\n";
            }
            else
            {
                script << "    steam -shutdown 2>/dev/null || true\n";
            }

            script << "    TIMEOUT=10\n";
            script << "    while [ $TIMEOUT -gt 0 ]; do\n";
            script << "        if ! pgrep -x steam >/dev/null 2>&1; then break; fi\n";
            script << "        sleep 1\n";
            script << "        TIMEOUT=$((TIMEOUT - 1))\n";
            script << "    done\n\n";

            script << "    if pgrep -x steam >/dev/null 2>&1; then\n";
            script << "        log \"Steam still active, sending SIGTERM...\"\n";
            script << "        pkill -TERM -x steam 2>/dev/null || true\n";
            script << "        sleep 3\n";
            script << "    fi\n\n";

            script << "    if pgrep -x steam >/dev/null 2>&1; then\n";
            script << "        log \"Steam still active, sending SIGKILL as last resort...\"\n";
            script << "        pkill -9 -x steam 2>/dev/null || true\n";
            script << "        sleep 2\n";
            script << "    fi\n\n";

            script << "    while pgrep -x steam >/dev/null 2>&1; do sleep 1; done\n";
            script << "    log \"Steam processes fully terminated.\"\n";
            script << "else\n";
            script << "    log \"Test environment: skipping Steam termination.\"\n";
            script << "fi\n\n";

            // 2. Back up user config
            script << "CONFIG_DIR=\"$HOME/.config/SLSsteam\"\n";
            script << "if [ -f \"$CONFIG_DIR/config.yaml\" ]; then\n";
            script << "    mkdir -p \"$CONFIG_DIR/backups\"\n";
            script << "    cp -a \"$CONFIG_DIR/config.yaml\" \"$CONFIG_DIR/backups/config_$(date +%Y%m%d_%H%M%S).yaml.bak\" 2>/dev/null || true\n";
            script << "fi\n\n";

            // 2.5 Back up cache directories (luas and manifest directories are strictly preserved and untouched)
            script << "CACHE_BACKUP_DIR=\"$HOME/.local/share/SLSsteam/cache_backups/cache_$(date +%Y%m%d_%H%M%S)\"\n";
            script << "mkdir -p \"$CACHE_BACKUP_DIR\"\n";
            script << "for cdir in \"$HOME/.local/share/ACCELA/depots\" \"$HOME/.local/share/SLSsteam/cache\"; do\n";
            script << "    if [ -d \"$cdir\" ]; then\n";
            script << "        mkdir -p \"$CACHE_BACKUP_DIR/$(basename \"$cdir\")\"\n";
            script << "        cp -a \"$cdir/.\" \"$CACHE_BACKUP_DIR/$(basename \"$cdir\")/\" 2>/dev/null || true\n";
            script << "    fi\n";
            script << "done\n\n";

            // 3. Backup existing installation for rollback
            std::string backupDir = installDir + ".backup_" + std::to_string(timestamp);
            script << "BACKUP_DIR=\"" << backupDir << "\"\n";
            script << "INSTALL_DIR=\"" << installDir << "\"\n";
            script << "ROLLBACK_NEEDED=0\n\n";

            script << "if [ -d \"$INSTALL_DIR\" ]; then\n";
            script << "    log \"Creating backup of existing installation at $BACKUP_DIR...\"\n";
            script << "    cp -a \"$INSTALL_DIR\" \"$BACKUP_DIR\" || {\n";
            script << "        log \"Backup failed! Aborting update.\"\n";
            script << "        [ \"${SLS_TEST_ENV:-0}\" != \"1\" ] && notify-send -u critical \"SLSsteam\" \"Update failed: Could not create backup.\"\n";
            script << "        exit 1\n";
            script << "    }\n";
            script << "fi\n\n";

            // 4. Run release installer while Steam is terminated
            script << "log \"Running release installer...\"\n";
            script << "cd \"" << releaseRoot << "\"\n";
            script << "chmod +x ./install.sh\n";
            script << "if ! ./install.sh; then\n";
            script << "    log \"Release installer failed!\"\n";
            script << "    ROLLBACK_NEEDED=1\n";
            script << "fi\n\n";

            // 5. Post-install verification
            script << "if [ $ROLLBACK_NEEDED -eq 0 ]; then\n";
            script << "    if [ ! -s \"$INSTALL_DIR/SLSsteam.so\" ] || [ ! -s \"$INSTALL_DIR/library-inject.so\" ]; then\n";
            script << "        log \"Post-install verification failed: required libraries missing or empty!\"\n";
            script << "        ROLLBACK_NEEDED=1\n";
            script << "    fi\n";
            script << "fi\n\n";

            // 6. Rollback or finalize
            script << "if [ $ROLLBACK_NEEDED -ne 0 ]; then\n";
            script << "    log \"Update failed! Rolling back to backup...\"\n";
            script << "    [ \"${SLS_TEST_ENV:-0}\" != \"1\" ] && notify-send -u critical \"SLSsteam\" \"Update failed! Rolling back...\"\n";
            script << "    if [ -d \"$BACKUP_DIR\" ]; then\n";
            script << "        rm -rf \"$INSTALL_DIR\"\n";
            script << "        mv \"$BACKUP_DIR\" \"$INSTALL_DIR\"\n";
            script << "    fi\n";
            script << "else\n";
            script << "    log \"Update succeeded! Cleaning up backup and staging...\"\n";
            script << "    [ \"${SLS_TEST_ENV:-0}\" != \"1\" ] && notify-send -u normal \"SLSsteam\" \"SLSsteam & ACCELA updated successfully! Relaunching Steam...\"\n";
            script << "    rm -rf \"$BACKUP_DIR\"\n";
            script << "    rm -rf \"" << stagingDir << "\"\n";
            script << "fi\n\n";

            // 7. Relaunch Steam
            script << "if [ \"${SLS_TEST_ENV:-0}\" != \"1\" ]; then\n";
            script << "    sleep 1\n";
            if (isFlatpak)
            {
                script << "    nohup flatpak run com.valvesoftware.Steam </dev/null >/dev/null 2>&1 &\n";
            }
            else
            {
                script << "    nohup steam </dev/null >/dev/null 2>&1 &\n";
            }
            script << "else\n";
            script << "    log \"Test environment: skipping Steam restart.\"\n";
            script << "fi\n\n";

            script << "rm -f \"$0\" 2>/dev/null || true\n";
        }

        // Set executable permissions
        chmod(scriptPath.c_str(), 0755);

        pid_t scriptPid = fork();
        if (scriptPid == 0)
        {
            setsid();
            close(STDIN_FILENO);
            const char* testEnv = std::getenv("SLS_TEST_ENV");
            if (!testEnv || (std::string(testEnv) != "1" && std::string(testEnv) != "true"))
            {
                close(STDOUT_FILENO);
                close(STDERR_FILENO);
            }
            execlp("bash", "bash", scriptPath.c_str(), nullptr);
            _exit(127);
        }

        const char* testEnv = std::getenv("SLS_TEST_ENV");
        if (testEnv && (std::string(testEnv) == "1" || std::string(testEnv) == "true"))
        {
            int scriptStatus = 0;
            waitpid(scriptPid, &scriptStatus, 0);
            LOG_INFO("AutoUpdate: Test mode: updater script finished with status %d\n", scriptStatus);
        }

        LOG_INFO("AutoUpdate: Transactional updater script launched (PID %d). Steam will restart shortly.\n", scriptPid);
    }

    void checkAndPrompt()
    {
        // Run on a separate detached thread to ensure zero blocking during Steam startup sequence
        std::thread(doCheckAndPrompt).detach();
    }

    void checkAndPromptSync()
    {
        doCheckAndPrompt();
    }
}
