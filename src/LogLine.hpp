#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// One line of Geode's log file read back: "13:38:54.123 DEBUG [Main] [Geode]:   message"
namespace logline
{
  struct Line
  {
    std::string time;
    // Since midnight, the file's name has the day
    int64_t millis = 0;
    int severity = 0;
    std::string thread;
    std::string source;
    int nest = 0;
    std::string content;
  };

  // Nothing for a line that isn't the start of a log, like the second line of a multi-line one
  std::optional<Line> parse(std::string_view line);
}
