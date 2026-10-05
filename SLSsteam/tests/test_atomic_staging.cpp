#include "../src/atomic_file.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void testAtomicWriteAndCopy()
{
    std::cout << "[TEST] Running testAtomicWriteAndCopy...\n" << std::flush;
    const std::string testDir = "/tmp/sls_test_atomic_file";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string target = testDir + "/sub/dir/test.txt";

    // 1. Basic atomic write creating nested directories
    bool ok = AtomicFile::write(target, "Hello Atomic World!\n");
    assert(ok);
    assert(fs::exists(target));

    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        assert(line == "Hello Atomic World!");
    }

    // 2. Overwrite atomically
    ok = AtomicFile::write(target, "Updated Atomic Content!\n");
    assert(ok);
    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        assert(line == "Updated Atomic Content!");
    }

    // 3. Atomic copy
    const std::string copyDst = testDir + "/copy/test_copied.txt";
    ok = AtomicFile::copy(target, copyDst);
    assert(ok);
    assert(fs::exists(copyDst));
    {
        std::ifstream f(copyDst);
        std::string line;
        std::getline(f, line);
        assert(line == "Updated Atomic Content!");
    }

    // 4. Stream write
    const std::string streamDst = testDir + "/stream.txt";
    ok = AtomicFile::writeStream(streamDst, [](std::ostream& os) {
        os << "Line 1\nLine 2\n";
        return true;
    });
    assert(ok);
    {
        std::ifstream f(streamDst);
        std::string l1, l2;
        std::getline(f, l1);
        std::getline(f, l2);
        assert(l1 == "Line 1");
        assert(l2 == "Line 2");
    }

    // 5. Cleanup stale temp files helper
    std::ofstream dummyTmp(testDir + "/rogue.tmp");
    dummyTmp << "stale";
    dummyTmp.close();
    assert(fs::exists(testDir + "/rogue.tmp"));
    AtomicFile::cleanupStaleTempFiles(testDir);
    assert(!fs::exists(testDir + "/rogue.tmp"));

    fs::remove_all(testDir);
    std::cout << "[PASS] testAtomicWriteAndCopy completed successfully.\n" << std::flush;
}

static void testMoveOrMergeDirectory()
{
    std::cout << "[TEST] Running testMoveOrMergeDirectory...\n" << std::flush;
    const std::string testDir = "/tmp/sls_test_merge_dir";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string stagingDir = testDir + "/downloading/12345";
    const std::string commonDir  = testDir + "/common/AwesomeGame";

    // Setup existing game directory with an existing mod, a save file, and an older executable
    fs::create_directories(commonDir + "/saves");
    fs::create_directories(commonDir + "/bin");
    AtomicFile::write(commonDir + "/saves/save1.dat", "UserSaveData");
    AtomicFile::write(commonDir + "/custom_mod.cfg", "ModConfig=1");
    AtomicFile::write(commonDir + "/bin/game.exe", "OldExeVersion1");

    // Setup staging directory with updated executable and new game assets
    fs::create_directories(stagingDir + "/bin");
    fs::create_directories(stagingDir + "/data");
    AtomicFile::write(stagingDir + "/bin/game.exe", "NewExeVersion2");
    AtomicFile::write(stagingDir + "/data/level1.pak", "Level1Data");

    // Perform atomic move and merge
    bool ok = AtomicFile::moveOrMergeDirectory(stagingDir, commonDir);
    assert(ok);

    // Staging directory should be completely gone
    assert(!fs::exists(stagingDir));

    // Existing user files must be preserved!
    {
        std::ifstream f(commonDir + "/saves/save1.dat");
        std::string content;
        f >> content;
        assert(content == "UserSaveData");
    }
    {
        std::ifstream f(commonDir + "/custom_mod.cfg");
        std::string content;
        f >> content;
        assert(content == "ModConfig=1");
    }

    // Matching files must be overwritten with new version!
    {
        std::ifstream f(commonDir + "/bin/game.exe");
        std::string content;
        f >> content;
        assert(content == "NewExeVersion2");
    }

    // New files must be present!
    assert(fs::exists(commonDir + "/data/level1.pak"));

    fs::remove_all(testDir);
    std::cout << "[PASS] testMoveOrMergeDirectory completed successfully.\n" << std::flush;
}

static void testCancellationSafety()
{
    std::cout << "[TEST] Running testCancellationSafety...\n" << std::flush;
    const std::string testDir = "/tmp/sls_test_cancel_safety";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string stagingDir = testDir + "/downloading/99999";
    const std::string commonDir  = testDir + "/common/MyGame";

    // Existing installed game files
    fs::create_directories(commonDir);
    AtomicFile::write(commonDir + "/game.exe", "OriginalCleanExe");
    AtomicFile::write(commonDir + "/data.bin", "OriginalCleanData");

    // Download starts into staging directory
    fs::create_directories(stagingDir);
    AtomicFile::write(stagingDir + "/partial_chunk.tmp", "IncompleteData");

    // User CANCELS download:
    // Only the staging directory is deleted!
    AtomicFile::removePath(stagingDir);

    // Verify staging is removed
    assert(!fs::exists(stagingDir));

    // Verify original game directory is completely untouched and uncorrupted!
    assert(fs::exists(commonDir + "/game.exe"));
    assert(fs::exists(commonDir + "/data.bin"));
    {
        std::ifstream f(commonDir + "/game.exe");
        std::string content;
        f >> content;
        assert(content == "OriginalCleanExe");
    }
    {
        std::ifstream f(commonDir + "/data.bin");
        std::string content;
        f >> content;
        assert(content == "OriginalCleanData");
    }

    fs::remove_all(testDir);
    std::cout << "[PASS] testCancellationSafety completed successfully.\n" << std::flush;
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "Running Atomic Staging Unit Tests\n";
    std::cout << "========================================\n";

    testAtomicWriteAndCopy();
    testMoveOrMergeDirectory();
    testCancellationSafety();

    std::cout << "\nALL ATOMIC STAGING TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
