#include "update_checker.hpp"

#include "platform.hpp"
#include "process.hpp"

#include <array>
#include <cctype>
#include <limits>

namespace {

std::optional<std::array<unsigned int, 3>> parseVersion(const std::string& value)
{
    size_t i = !value.empty() && (value[0] == 'v' || value[0] == 'V') ? 1 : 0;
    std::array<unsigned int, 3> parts{};
    for (size_t part = 0; part < parts.size(); ++part) {
        if (i >= value.size() || !std::isdigit(static_cast<unsigned char>(value[i]))) return std::nullopt;
        unsigned int number = 0;
        while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
            const unsigned int digit = static_cast<unsigned int>(value[i++] - '0');
            if (number > (std::numeric_limits<unsigned int>::max() - digit) / 10) return std::nullopt;
            number = number * 10 + digit;
        }
        parts[part] = number;
        if (part < 2 && (i >= value.size() || value[i++] != '.')) return std::nullopt;
    }
    return i == value.size() ? std::optional(parts) : std::nullopt;
}

std::optional<std::string> jsonStringField(const std::string& json, const std::string& name)
{
    size_t pos = json.find('"' + name + '"');
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + name.size() + 2);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos++] != '"') return std::nullopt;
    const size_t end = json.find('"', pos);
    if (end == std::string::npos || json.substr(pos, end - pos).find('\\') != std::string::npos) return std::nullopt;
    return json.substr(pos, end - pos);
}

} // namespace

bool isNewerVersion(const std::string& candidate, const std::string& current)
{
    const auto next = parseVersion(candidate);
    const auto installed = parseVersion(current);
    return next && installed && *next > *installed;
}

std::optional<ReleaseInfo> parseLatestRelease(const std::string& json)
{
    const auto tag = jsonStringField(json, "tag_name");
    if (!tag || !parseVersion(*tag)) return std::nullopt;
    return ReleaseInfo{*tag, "https://github.com/WiiListZ/dwc-server-advanced-patcher/releases/tag/" + *tag};
}

UpdateCheckResult checkForUpdates(const std::string& current)
{
#ifdef _WIN32
    const char* curl = "curl.exe";
#else
    const char* curl = "curl";
#endif
    const CaptureResult response = runCapture(
        {curl, "--fail", "--location", "--silent", "--show-error", "--max-time", "12",
         "--header", "Accept: application/vnd.github+json",
         "--header", "User-Agent: dwc-server-advanced-patcher",
         "https://api.github.com/repos/WiiListZ/dwc-server-advanced-patcher/releases/latest"}, executableDir());
    if (response.exitCode != 0) {
        return {false, false, {}, "Could not check GitHub releases. Check your connection and try again."};
    }
    const auto release = parseLatestRelease(response.output);
    if (!release) return {false, false, {}, "GitHub returned an unreadable release version."};
    return {true, isNewerVersion(release->version, current), *release, {}};
}
