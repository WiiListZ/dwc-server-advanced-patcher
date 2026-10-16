#pragma once

#include <filesystem>
#include <string>

#include "version.hpp"

struct SourceInstallResult {
    bool ok = false;
    std::string message;
};

// Downloads the source archive for this GUI's release and copies it into
// target. Existing files are never overwritten, so target may also be the
// directory containing the GUI executable.
SourceInstallResult installPatcherSource(const std::filesystem::path& target,
                                         const std::string& archiveUrl =
                                             std::string("https://codeload.github.com/WiiListZ/dwc-server-advanced-patcher/tar.gz/refs/tags/v") + kPatcherVersion);
