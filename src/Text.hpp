#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace text
{
  // The line with every number swapped for '#': "pos 12.5, -3" and "pos 13, -2.25" look the same
  std::string withoutNumbers(std::string_view line);

  // Columns the text takes in the console, CJK and emoji take two
  int width(std::string_view utf8);

  std::vector<std::string_view> splitLines(std::string_view content);

  // A steady color for a mod, from a set that reads well on a dark console
  int colorFor(std::string_view key);
}
