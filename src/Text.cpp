#include "Text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace text
{
  static bool isDigit(char c)
  {
    return c >= '0' && c <= '9';
  }

  static bool isHexDigit(char c)
  {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  std::string withoutNumbers(std::string_view line)
  {
    std::string out;
    out.reserve(line.size());

    size_t i = 0;
    while (i < line.size())
    {
      // Pointers and other hex values
      if (line[i] == '0' && i + 2 < line.size() && (line[i + 1] == 'x' || line[i + 1] == 'X') && isHexDigit(line[i + 2]))
      {
        i += 2;
        while (i < line.size() && isHexDigit(line[i]))
          i++;
        out.push_back('#');
        continue;
      }

      if (isDigit(line[i]))
      {
        if (!out.empty() && out.back() == '-')
          out.pop_back();
        while (i < line.size() && isDigit(line[i]))
          i++;
        // 12.5 is one number, the dot at the end of a sentence is not
        if (i + 1 < line.size() && line[i] == '.' && isDigit(line[i + 1]))
        {
          i++;
          while (i < line.size() && isDigit(line[i]))
            i++;
        }
        out.push_back('#');
        continue;
      }

      out.push_back(line[i]);
      i++;
    }

    return out;
  }

  static bool isWide(uint32_t cp)
  {
    return (cp >= 0x1100 && cp <= 0x115F) ||
           (cp >= 0x2E80 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
  }

  // Reads one code point at i and moves i past it
  static uint32_t next(std::string_view utf8, size_t& i)
  {
    auto byte = static_cast<uint8_t>(utf8[i]);
    uint32_t cp = byte;
    size_t length = 1;
    if (byte >= 0xF0)
    {
      cp = byte & 0x07;
      length = 4;
    }
    else if (byte >= 0xE0)
    {
      cp = byte & 0x0F;
      length = 3;
    }
    else if (byte >= 0xC0)
    {
      cp = byte & 0x1F;
      length = 2;
    }

    for (size_t k = 1; k < length && i + k < utf8.size(); k++)
      cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3F);
    i = std::min(i + length, utf8.size());
    return cp;
  }

  static int widthOf(uint32_t cp)
  {
    if (cp == '\t')
      return 4;
    if (cp < 0x20)
      return 0;
    return isWide(cp) ? 2 : 1;
  }

  int width(std::string_view utf8)
  {
    int columns = 0;
    size_t i = 0;
    while (i < utf8.size())
      columns += widthOf(next(utf8, i));
    return columns;
  }

  std::string_view fit(std::string_view utf8, int columns)
  {
    int used = 0;
    size_t i = 0;
    while (i < utf8.size())
    {
      size_t start = i;
      used += widthOf(next(utf8, i));
      if (used > columns)
        return utf8.substr(0, start);
    }
    return utf8;
  }

  std::vector<std::string> wrap(std::string_view content, int columns)
  {
    std::vector<std::string> out;
    columns = std::max(columns, 1);
    for (auto line : splitLines(content))
    {
      do
      {
        auto part = fit(line, columns);
        // A character wider than the whole line still has to go somewhere
        if (part.empty() && !line.empty())
        {
          size_t i = 0;
          next(line, i);
          part = line.substr(0, i);
        }
        out.emplace_back(part);
        line.remove_prefix(part.size());
      } while (!line.empty());
    }
    return out;
  }

  std::string clean(std::string_view content)
  {
    std::string out;
    out.reserve(content.size());
    for (size_t i = 0; i < content.size(); i++)
    {
      char c = content[i];
      if (c == '\x1b')
      {
        // CSI sequences like colors end with a letter
        if (i + 1 < content.size() && content[i + 1] == '[')
        {
          i += 2;
          while (i < content.size() && !(content[i] >= '@' && content[i] <= '~'))
            i++;
        }
        continue;
      }
      if (c == '\t')
        out += "    ";
      else if (c == '\n' || static_cast<uint8_t>(c) >= 0x20)
        out.push_back(c);
    }
    return out;
  }

  static char lower(char c)
  {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  size_t findCaseless(std::string_view text, std::string_view part, size_t from)
  {
    if (part.empty())
      return from <= text.size() ? from : std::string_view::npos;
    for (size_t i = from; i + part.size() <= text.size(); i++)
    {
      size_t k = 0;
      while (k < part.size() && lower(text[i + k]) == lower(part[k]))
        k++;
      if (k == part.size())
        return i;
    }
    return std::string_view::npos;
  }

  bool containsCaseless(std::string_view text, std::string_view part)
  {
    return findCaseless(text, part) != std::string_view::npos;
  }

  std::vector<std::string_view> splitLines(std::string_view content)
  {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (true)
    {
      auto end = content.find('\n', start);
      auto line = content.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      lines.push_back(line);
      if (end == std::string_view::npos)
        break;
      start = end + 1;
    }

    // A log that ends with a newline doesn't need an empty line after it
    while (lines.size() > 1 && lines.back().empty())
      lines.pop_back();
    return lines;
  }

  int colorFor(std::string_view key)
  {
    static constexpr std::array<int, 20> palette = {
        39, 43, 75, 81, 99, 111, 117, 141, 147, 171,
        177, 183, 213, 215, 221, 155, 119, 85, 209, 167};

    // FNV-1a, the same mod keeps its color between launches
    uint32_t hash = 2166136261u;
    for (char c : key)
    {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return palette[hash % palette.size()];
  }
}
