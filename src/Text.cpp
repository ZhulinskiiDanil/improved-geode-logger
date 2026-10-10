#include "Text.hpp"

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

  int width(std::string_view utf8)
  {
    int columns = 0;
    size_t i = 0;
    while (i < utf8.size())
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
      i += length;

      if (cp == '\t')
        columns += 4;
      else if (cp >= 0x20)
        columns += isWide(cp) ? 2 : 1;
    }
    return columns;
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
