#include "LogLine.hpp"
#include "Levels.hpp"

#include <charconv>

namespace logline
{
  static bool isDigit(char c)
  {
    return c >= '0' && c <= '9';
  }

  static int severityOf(std::string_view name)
  {
    if (name == "TRACE")
      return level::Trace;
    if (name == "DEBUG")
      return level::Debug;
    if (name == "WARN ")
      return level::Warning;
    if (name == "ERROR")
      return level::Error;
    return level::Info;
  }

  // The thread and the milliseconds may be missing
  std::optional<Line> parse(std::string_view text)
  {
    if (text.size() < 15 || !isDigit(text[0]) || !isDigit(text[1]) || text[2] != ':' || text[5] != ':' ||
        !isDigit(text[3]) || !isDigit(text[4]) || !isDigit(text[6]) || !isDigit(text[7]))
      return std::nullopt;

    Line line;
    auto space = text.find(' ');
    if (space == std::string_view::npos)
      return std::nullopt;
    line.time = std::string(text.substr(0, space));

    int hours = (text[0] - '0') * 10 + (text[1] - '0');
    int minutes = (text[3] - '0') * 10 + (text[4] - '0');
    int seconds = (text[6] - '0') * 10 + (text[7] - '0');
    int millis = 0;
    if (space > 9 && text[8] == '.')
      std::from_chars(text.data() + 9, text.data() + space, millis);
    line.millis = ((hours * 60 + minutes) * 60 + seconds) * 1000LL + millis;

    auto rest = text.substr(space + 1);
    if (rest.size() < 7)
      return std::nullopt;
    line.severity = severityOf(rest.substr(0, 5));
    rest.remove_prefix(6);

    // One bracket is the mod, two are the thread and the mod
    auto bracket = [&](std::string& out) -> bool
    {
      if (rest.empty() || rest[0] != '[')
        return false;
      auto close = rest.find(']');
      if (close == std::string_view::npos)
        return false;
      out = std::string(rest.substr(1, close - 1));
      rest.remove_prefix(close + 1);
      return true;
    };

    std::string first;
    if (!bracket(first))
      return std::nullopt;
    if (rest.starts_with(" ["))
    {
      rest.remove_prefix(1);
      line.thread = std::move(first);
      if (!bracket(line.source))
        return std::nullopt;
    }
    else
      line.source = std::move(first);

    if (!rest.starts_with(": "))
      return std::nullopt;
    rest.remove_prefix(2);

    size_t spaces = 0;
    while (spaces < rest.size() && rest[spaces] == ' ')
      spaces++;
    line.nest = static_cast<int>(spaces + 1) / 2;
    line.content = std::string(rest.substr(spaces));
    return line;
  }
}
