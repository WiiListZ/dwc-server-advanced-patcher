#pragma once

#include <string>
#include <string_view>

// Read the server domain embedded in a generated stage 1 binary or Gecko code.
// Empty means the URL could not be found.
std::string stage1ServerDomain(std::string_view binary);
std::string geckoServerDomain(std::string_view code);
