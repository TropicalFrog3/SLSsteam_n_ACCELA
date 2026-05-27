#include "autoupdate.hpp"
#include "../curl.hpp"
#include "../log.hpp"
#include "../version.hpp"

#include <yaml-cpp/yaml.h>
#include <curl/curl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace AutoUpdate
{
    static const char* REMOTE_VERSION_URL = "https://raw.githubusercontent.com/TropicalFrog3/SLSsteam_n_ACCELA/refs/heads/main/SLSsteam/res/version";

    static size_t curlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* file = static_cast<FILE*>(userdata);
        return fwrite(ptr, size, nmemb, file);
    }

    static bool downloadToFile(const std::string& url, const std::string& destPath)
    {
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

    static void doCheckAndPrompt()
    {
        g_pLog->info("AutoUpdate: Checking for updates...\n");

        std::string data;
        int res = Curl::getString(REMOTE_VERSION_URL, data);
        if (res != 0)
        {
            g_pLog->debug("AutoUpdate: Failed to fetch remote version (curl code %d)\n", res);
            return;
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
            g_pLog->warn("AutoUpdate: Failed to parse remote version manifest: %s\n", e.what());
            return;
        }

        if (!isRemoteNewer(VERSION, remoteVersionStr))
        {
            g_pLog->info("AutoUpdate: Already up to date (Local: %s, Remote: %s)\n", VERSION, remoteVersionStr.c_str());
            return;
        }

        g_pLog->info("AutoUpdate: Newer version available (Local: %s, Remote: %s)\n", VERSION, remoteVersionStr.c_str());

        // Replace version placeholder in DownloadUrl
        size_t pos = downloadUrl.find("${version}");
        if (pos != std::string::npos)
        {
            downloadUrl.replace(pos, 10, remoteVersionStr);
        }

        // Prepare prompt text
        std::string text = std::string(VERSION) + " -> " + remoteVersionStr + 
                           " newer update is available, would you like to update now?\n\nDetails of the update:\n" + changelog;

        // Fork to launch zenity dialog safely
        pid_t pid = fork();
        if (pid < 0)
        {
            g_pLog->warn("AutoUpdate: fork() failed for update prompt\n");
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
            g_pLog->warn("AutoUpdate: waitpid() failed on prompt\n");
            return;
        }

        if (!WIFEXITED(status))
        {
            return;
        }

        int exitStatus = WEXITSTATUS(status);
        if (exitStatus == 127)
        {
            g_pLog->info("AutoUpdate: zenity not found. Falling back to notify-send alert.\n");
            system("notify-send -u normal \"SLSsteam\" \"Update available! Newer version is ready. Run setup.sh to update.\"");
            return;
        }

        if (exitStatus != 0)
        {
            // User cancelled/closed the dialog
            g_pLog->info("AutoUpdate: User declined the update.\n");
            return;
        }

        // User clicked "Install"
        g_pLog->info("AutoUpdate: User accepted update. Starting download...\n");
        system("notify-send -t 5000 \"SLSsteam\" \"Downloading update...\"");

        // Determine destination folder
        const char* home = getenv("HOME");
        if (!home)
        {
            g_pLog->warn("AutoUpdate: HOME environment variable not set, aborting update.\n");
            system("notify-send -u critical \"SLSsteam\" \"Update failed: HOME environment variable not found.\"");
            return;
        }

        std::string installDir = std::string(home) + "/.local/share/SLSsteam";
        if (std::filesystem::exists(std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam/SLSsteam.so"))
        {
            installDir = std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam";
        }

        std::string ext = ".zip";
        if (downloadUrl.find(".7z") != std::string::npos)
        {
            ext = ".7z";
        }

        std::string archivePath = "/tmp/SLSsteam_update" + ext;
        std::string extractDir = "/tmp/SLSsteam_update_extracted";

        if (!downloadToFile(downloadUrl, archivePath))
        {
            g_pLog->warn("AutoUpdate: Failed to download update from %s\n", downloadUrl.c_str());
            system("notify-send -u critical \"SLSsteam\" \"Update failed: Download failed.\"");
            return;
        }

        if (std::filesystem::exists(extractDir))
        {
            std::filesystem::remove_all(extractDir);
        }
        std::filesystem::create_directories(extractDir);

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
                // Try 7z fallback for zip
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
            g_pLog->warn("AutoUpdate: Extraction failed for %s\n", archivePath.c_str());
            system("notify-send -u critical \"SLSsteam\" \"Update failed: Extraction tools not found or failed.\"");
            std::filesystem::remove(archivePath);
            std::filesystem::remove_all(extractDir);
            return;
        }

        // Instead of copying .so files while Steam is running (which crashes it),
        // we write a helper script that kills Steam first, then copies, then relaunches.
        g_pLog->info("AutoUpdate: Preparing update installer script...\n");
        system("notify-send -u normal \"SLSsteam\" \"Update downloaded! Applying update, please wait...\"");

        // Build the helper script that runs after Steam exits
        // IMPORTANT: Script name must NOT contain "steam" to avoid pkill/pgrep self-match
        std::string scriptPath = "/tmp/sls_patcher.sh";
        {
            std::ofstream script(scriptPath);
            if (!script.is_open())
            {
                g_pLog->warn("AutoUpdate: Failed to create update script at %s\n", scriptPath.c_str());
                system("notify-send -u critical \"SLSsteam\" \"Update failed: Could not create update script.\"");
                std::filesystem::remove(archivePath);
                std::filesystem::remove_all(extractDir);
                return;
            }

            script << "#!/bin/bash\n";
            script << "# Auto-generated patcher script\n";
            script << "sleep 1\n";

            // Gracefully shut down Steam, then force-kill if needed
            script << "steam -shutdown 2>/dev/null || true\n";
            script << "sleep 5\n";
            // Force-kill any remaining steam processes (but exclude this script via grep -v)
            script << "pkill -9 -x steam 2>/dev/null || true\n";
            script << "sleep 2\n";
            // Wait until the main steam binary is gone (match exact name, not this script)
            script << "while pgrep -x steam > /dev/null 2>&1; do sleep 1; done\n";
            script << "sleep 1\n";

            // Copy all files from extract dir to install dir
            for (const auto& entry : std::filesystem::recursive_directory_iterator(extractDir))
            {
                if (!entry.is_regular_file()) continue;

                std::string pathStr = entry.path().string();
                std::string filename = entry.path().filename().string();
                std::filesystem::path destPath;

                if (filename == "SLSsteam.so")
                {
                    destPath = std::filesystem::path(installDir) / "SLSsteam.so";
                }
                else if (filename == "library-inject.so")
                {
                    destPath = std::filesystem::path(installDir) / "library-inject.so";
                }
                else
                {
                    // Check if it belongs in the 'res' folder
                    size_t resPos = pathStr.find("/res/");
                    if (resPos != std::string::npos)
                    {
                        std::string relativeResPath = pathStr.substr(resPos + 5);
                        destPath = std::filesystem::path(installDir) / "res" / relativeResPath;
                    }
                }

                if (!destPath.empty())
                {
                    // Ensure parent directory exists
                    script << "mkdir -p \"" << destPath.parent_path().string() << "\"\n";
                    script << "cp -f \"" << entry.path().string() << "\" \"" << destPath.string() << "\"\n";
                    g_pLog->info("AutoUpdate: Queued copy %s -> %s\n", filename.c_str(), destPath.c_str());
                }
            }

            // Clean up temp files
            script << "rm -f \"" << archivePath << "\"\n";
            script << "rm -rf \"" << extractDir << "\"\n";

            // Notify and relaunch Steam
            script << "notify-send -u normal \"SLSsteam\" \"Update installed! Relaunching Steam...\"\n";
            script << "sleep 1\n";
            script << "nohup steam </dev/null >/dev/null 2>&1 &\n";

            // Self-delete
            script << "rm -f \"" << scriptPath << "\"\n";
        }

        // Make executable and launch detached
        chmod(scriptPath.c_str(), 0755);

        pid_t scriptPid = fork();
        if (scriptPid == 0)
        {
            // Child: detach completely and run the update script
            setsid();
            // Close inherited file descriptors to fully detach from Steam
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            execlp("bash", "bash", scriptPath.c_str(), nullptr);
            _exit(127);
        }

        g_pLog->info("AutoUpdate: Update script launched (PID %d). Steam will restart shortly.\n", scriptPid);
    }

    void checkAndPrompt()
    {
        // Run on a separate detached thread to ensure zero blocking during Steam startup sequence
        std::thread(doCheckAndPrompt).detach();
    }
}
