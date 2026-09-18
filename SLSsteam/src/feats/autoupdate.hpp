#pragma once

namespace AutoUpdate
{
    // Check for updates and prompt the user if one is available.
    // Runs on a detached background thread so it doesn't block Steam startup.
    void checkAndPrompt();

    // Synchronous execution for testing and verification without detached thread.
    void checkAndPromptSync();
}
