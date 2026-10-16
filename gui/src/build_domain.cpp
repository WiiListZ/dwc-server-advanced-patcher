#include "build_domain.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string domainAfterNasUrl(std::string_view bytes)
{
    constexpr std::string_view marker = "://nas.";
    const size_t found = bytes.find(marker);
    if (found == std::string_view::npos) return {};
    const size_t start = found + marker.size();
    const size_t end = bytes.find('/', start);
    if (end == std::string_view::npos || end == start || end - start > 200) return {};
    const std::string_view domain = bytes.substr(start, end - start);
    if (!std::all_of(domain.begin(), domain.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-';
        })) return {};
    return std::string(domain);
}

} // namespace

std::string stage1ServerDomain(std::string_view binary)
{
    return domainAfterNasUrl(binary);
}

std::string geckoServerDomain(std::string_view code)
{
    // Gecko text is pairs of 32-bit hex words. Decode them to find the NAS URL.
    std::istringstream words{std::string(code)};
    std::string word;
    std::string bytes;
    while (words >> word) {
        if (word.size() != 8 || !std::all_of(word.begin(), word.end(), [](unsigned char c) { return std::isxdigit(c); })) continue;
        const unsigned long value = std::stoul(word, nullptr, 16);
        for (int shift : {24, 16, 8, 0}) bytes.push_back(static_cast<char>(value >> shift));
    }
    return domainAfterNasUrl(bytes);
}
