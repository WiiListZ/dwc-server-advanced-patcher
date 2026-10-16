#include "update_checker.hpp"

int main()
{
    if (!isNewerVersion("v1.0.4", "1.0.3")) return 1;
    if (!isNewerVersion("v1.1.0", "1.0.99")) return 2;
    if (isNewerVersion("v1.0.3", "1.0.3")) return 3;
    if (isNewerVersion("v1.0.2", "1.0.3")) return 4;
    if (isNewerVersion("v1.0.4-rc1", "1.0.3")) return 5;
    if (isNewerVersion("unknown", "1.0.3")) return 6;
    const auto release = parseLatestRelease(R"({"tag_name": "v1.0.4", "body": "Update"})");
    if (!release || release->version != "v1.0.4") return 7;
    if (release->url != "https://github.com/WiiListZ/dwc-server-advanced-patcher/releases/tag/v1.0.4") return 8;
    if (parseLatestRelease(R"({"tag_name": "not-a-version"})")) return 9;
    if (parseLatestRelease(R"({"message": "Not Found"})")) return 10;
}
