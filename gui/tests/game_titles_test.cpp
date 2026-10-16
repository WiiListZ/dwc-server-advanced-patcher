#include "game_titles.hpp"

#include <iterator>
#include <string_view>

int main()
{
    std::string_view disc, channel;
    for (size_t i = 0; i < std::size(kGameTitles); ++i) {
        if (i && !(kGameTitles[i - 1].first < kGameTitles[i].first)) return 1;
        if (kGameTitles[i].first == "RMCPD") disc = kGameTitles[i].second;
        if (kGameTitles[i].first == "RMCPN") channel = kGameTitles[i].second;
    }
    if (disc != "Mario Kart Wii") return 2;
    if (channel != "Mario Kart Channel") return 3;
    if (std::size(kGameTitles) < 450) return 4;
}
