#pragma once

#include <optional>
#include <string>

struct ReleaseInfo {
    std::string version;
    std::string url;
};

struct UpdateCheckResult {
    bool ok = false;
    bool available = false;
    ReleaseInfo release;
    std::string error;
};

bool isNewerVersion(const std::string& candidate, const std::string& current);
std::optional<ReleaseInfo> parseLatestRelease(const std::string& json);
UpdateCheckResult checkForUpdates(const std::string& current);
