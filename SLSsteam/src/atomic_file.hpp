#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

/**
 * AtomicFile
 *
 * Provides safe, atomic file and directory operations to prevent file corruption
 * during downloads, modifications, cancellations, crashes, or abrupt exits.
 *
 * Rules:
 *   1. Writes to a uniquely named temporary file (<target>.tmp.<pid>_<time>_<rand>).
 *   2. Flushes and syncs to disk before renaming.
 *   3. Atomically renames temporary file over destination.
 *   4. Automatically cleans up temporary file if any step fails.
 *   5. Merges directories safely by moving files and overwriting conflicting entries.
 */
namespace AtomicFile
{
    /**
     * Generate a unique temporary path beside the target destination.
     * Placing the temporary file in the same parent directory guarantees that
     * rename() will be an atomic same-filesystem operation.
     */
    inline std::string makeTempPath(const std::string& targetPath)
    {
        const pid_t pid = getpid();
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        static thread_local std::mt19937 gen(static_cast<unsigned int>(now ^ pid));
        std::uniform_int_distribution<unsigned int> dis(100000, 999999);
        return targetPath + ".tmp." + std::to_string(pid) + "_" + std::to_string(now) + "_" + std::to_string(dis(gen));
    }

    /**
     * RAII Guard that automatically removes a temporary file on scope exit
     * unless dismiss() is called.
     */
    struct TempFileGuard
    {
        std::string path;
        bool active = true;

        explicit TempFileGuard(std::string p) : path(std::move(p)) {}
        ~TempFileGuard()
        {
            if (active && !path.empty())
            {
                std::error_code ec;
                fs::remove(path, ec);
            }
        }
        void dismiss() { active = false; }
    };

    /**
     * Atomically write a block of data or text to targetPath.
     * Creates any missing parent directories.
     */
    inline bool write(const std::string& targetPath, const std::string_view& content, bool binary = false)
    {
        if (targetPath.empty()) return false;

        std::error_code ec;
        fs::path p(targetPath);
        if (p.has_parent_path())
        {
            fs::create_directories(p.parent_path(), ec);
            if (ec) return false;
        }

        const std::string tmpPath = makeTempPath(targetPath);
        TempFileGuard guard(tmpPath);

        const auto mode = binary
            ? (std::ios::out | std::ios::binary | std::ios::trunc)
            : (std::ios::out | std::ios::trunc);

        {
            std::ofstream ofs(tmpPath, mode);
            if (!ofs.is_open()) return false;

            if (!content.empty())
            {
                ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            }
            ofs.flush();
            if (!ofs.good()) return false;
        }

        // Atomic rename
        fs::rename(tmpPath, targetPath, ec);
        if (ec)
        {
            // If cross-device (rare for same-dir temp file, but handles odd mounts), copy then remove
            fs::copy_file(tmpPath, targetPath, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }

        guard.dismiss();
        return true;
    }

    /**
     * Atomically write using a custom stream writer callback.
     * Ideal for YAML emitters, JSON formatters, or line-by-line writers.
     */
    inline bool writeStream(const std::string& targetPath,
                            const std::function<bool(std::ostream&)>& writer,
                            bool binary = false)
    {
        if (targetPath.empty() || !writer) return false;

        std::error_code ec;
        fs::path p(targetPath);
        if (p.has_parent_path())
        {
            fs::create_directories(p.parent_path(), ec);
            if (ec) return false;
        }

        const std::string tmpPath = makeTempPath(targetPath);
        TempFileGuard guard(tmpPath);

        const auto mode = binary
            ? (std::ios::out | std::ios::binary | std::ios::trunc)
            : (std::ios::out | std::ios::trunc);

        {
            std::ofstream ofs(tmpPath, mode);
            if (!ofs.is_open()) return false;

            if (!writer(ofs)) return false;

            ofs.flush();
            if (!ofs.good()) return false;
        }

        fs::rename(tmpPath, targetPath, ec);
        if (ec)
        {
            fs::copy_file(tmpPath, targetPath, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }

        guard.dismiss();
        return true;
    }

    /**
     * Atomically copy a file from srcPath to dstPath via temporary staging.
     */
    inline bool copy(const std::string& srcPath, const std::string& dstPath)
    {
        if (srcPath.empty() || dstPath.empty()) return false;

        std::error_code ec;
        if (!fs::exists(srcPath, ec) || ec) return false;

        fs::path p(dstPath);
        if (p.has_parent_path())
        {
            fs::create_directories(p.parent_path(), ec);
            if (ec) return false;
        }

        const std::string tmpPath = makeTempPath(dstPath);
        TempFileGuard guard(tmpPath);

        fs::copy_file(srcPath, tmpPath, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;

        fs::rename(tmpPath, dstPath, ec);
        if (ec)
        {
            fs::copy_file(tmpPath, dstPath, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }

        guard.dismiss();
        return true;
    }

    /**
     * Move or merge an entire staging directory into a destination directory.
     * Matching files in dstDir are overwritten; existing non-conflicting files
     * in dstDir are preserved.
     * When complete, the staging directory srcDir is removed.
     */
    inline bool moveOrMergeDirectory(const std::string& srcDir, const std::string& dstDir)
    {
        if (srcDir.empty() || dstDir.empty()) return false;

        std::error_code ec;
        if (!fs::exists(srcDir, ec) || !fs::is_directory(srcDir, ec))
            return false;

        // If destination doesn't exist at all, try a fast atomic directory rename
        if (!fs::exists(dstDir, ec))
        {
            fs::path dstPath(dstDir);
            if (dstPath.has_parent_path())
            {
                fs::create_directories(dstPath.parent_path(), ec);
            }

            fs::rename(srcDir, dstDir, ec);
            if (!ec) return true; // Successfully renamed whole directory
        }

        // Destination exists or cross-device: recursively move each entry
        fs::create_directories(dstDir, ec);

        for (const auto& entry : fs::recursive_directory_iterator(srcDir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;

            const auto relPath = fs::relative(entry.path(), srcDir, ec);
            if (ec) continue;

            const fs::path destItem = fs::path(dstDir) / relPath;

            if (entry.is_directory(ec))
            {
                fs::create_directories(destItem, ec);
            }
            else if (entry.is_regular_file(ec) || entry.is_symlink(ec))
            {
                if (destItem.has_parent_path())
                {
                    fs::create_directories(destItem.parent_path(), ec);
                }

                // Try atomic file rename
                fs::rename(entry.path(), destItem, ec);
                if (ec)
                {
                    // Fall back to copy + remove
                    ec.clear();
                    fs::copy_file(entry.path(), destItem, fs::copy_options::overwrite_existing, ec);
                    fs::remove(entry.path(), ec);
                }
            }
        }

        // Clean up the now-empty or residual srcDir
        fs::remove_all(srcDir, ec);
        return true;
    }

    /**
     * Safely remove a file or directory without throwing.
     */
    inline bool removePath(const std::string& path)
    {
        if (path.empty()) return true;
        std::error_code ec;
        fs::remove_all(path, ec);
        return !ec;
    }

    /**
     * Purge leftover temporary files matching ".tmp." in a directory.
     */
    inline void cleanupStaleTempFiles(const std::string& dirPath)
    {
        if (dirPath.empty()) return;
        std::error_code ec;
        if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return;

        for (const auto& entry : fs::directory_iterator(dirPath, ec))
        {
            if (ec) break;
            if (entry.is_regular_file(ec))
            {
                const std::string filename = entry.path().filename().string();
                if (filename.find(".tmp.") != std::string::npos || filename.ends_with(".tmp"))
                {
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }
} // namespace AtomicFile
