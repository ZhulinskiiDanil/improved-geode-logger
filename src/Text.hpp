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

  // The longest start of the text that fits in this many columns
  std::string_view fit(std::string_view utf8, int columns);

  // Lines of at most this many columns, long lines broken where they don't fit
  std::vector<std::string> wrap(std::string_view content, int columns);

  // Tabs to spaces, escape codes and other control characters dropped, newlines kept
  std::string clean(std::string_view content);

  bool containsCaseless(std::string_view text, std::string_view part);

  // A steady color for a mod, from a set that reads well on a dark console
  int colorFor(std::string_view key);
}
