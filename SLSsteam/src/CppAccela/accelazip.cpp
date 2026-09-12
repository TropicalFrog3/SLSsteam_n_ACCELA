#include "accelazip.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace CppAccela::Zip
{
    // ── Validation ────────────────────────────────────────────────────────────

    bool isValid(const std::string& path)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;

        unsigned char magic[4] = {};
        const size_t n = fread(magic, 1, 4, fp);
        fclose(fp);

        if (n < 4) return false;

        // PK\x03\x04  normal local-file entry
        // PK\x05\x06  end-of-central-directory (empty archive)
        // PK\x07\x08  data descriptor (spanned/split)
        return magic[0] == 'P' && magic[1] == 'K' &&
               (magic[2] == 0x03 || magic[2] == 0x05 || magic[2] == 0x07);
    }

    // ── Assembly ──────────────────────────────────────────────────────────────

    bool assemble(const std::string& zipPath,
                  const std::string& luaPath,
                  const std::vector<std::string>& manifestPaths)
    {
        if (luaPath.empty() || manifestPaths.empty())
            return false;

        // Build argv:  zip -j -0 <zipPath> <luaPath> [manifests…]
        // We use execvp so no shell is involved (no injection risk).
        std::vector<const char*> argv;
        argv.reserve(4 + manifestPaths.size() + 1);
        argv.push_back("zip");
        argv.push_back("-j");   // junk paths (store basename only)
        argv.push_back("-0");   // no compression
        argv.push_back(zipPath.c_str());
        argv.push_back(luaPath.c_str());
        for (const auto& m : manifestPaths)
            argv.push_back(m.c_str());
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0)
        {
            // Redirect stdout/stderr to /dev/null — callers read no output
            FILE* devnull = fopen("/dev/null", "w");
            if (devnull)
            {
                dup2(fileno(devnull), STDOUT_FILENO);
                dup2(fileno(devnull), STDERR_FILENO);
                fclose(devnull);
            }
            execvp("zip", const_cast<char* const*>(argv.data()));
            _exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
        {
            fs::remove(zipPath);
            return false;
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            fs::remove(zipPath);
            return false;
        }

        return true;
    }

    // ── Extraction ────────────────────────────────────────────────────────────

    bool extract(const std::string& zipPath, const std::string& destDir)
    {
        fs::create_directories(destDir);

        pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0)
        {
            // unzip -o -q <zipPath> -d <destDir>
            execlp("unzip", "unzip",
                   "-o", "-q",
                   zipPath.c_str(),
                   "-d", destDir.c_str(),
                   nullptr);
            _exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return false;
        if (!WIFEXITED(status)) return false;
        return WEXITSTATUS(status) == 0;
    }

    // ── File discovery ────────────────────────────────────────────────────────

    ExtractedFiles findFiles(const std::string& extractDir,
                             const std::string& appId)
    {
        ExtractedFiles result;
        const std::string expectedLua = appId + ".lua";

        for (const auto& entry : fs::recursive_directory_iterator(extractDir))
        {
            if (!entry.is_regular_file()) continue;

            const std::string name = entry.path().filename().string();

            if (name == expectedLua)
            {
                result.luaFile = entry.path().string();
            }
            else if (name.size() > 9 &&
                     name.compare(name.size() - 9, 9, ".manifest") == 0)
            {
                result.manifestFiles.push_back(entry.path().string());
            }
        }

        return result;
    }

} // namespace CppAccela::Zip
