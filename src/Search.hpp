#pragma once

#include "Record.hpp"

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

// The search of the interactive screen. Words must all be found, in the message or the mod's name:
// -word must not be there, "a phrase" keeps its spaces, mod:, thread: and level: look at one thing,
// re: is a regex
namespace search
{
  struct Term
  {
    enum class Field
    {
      Text,
      Mod,
      Thread,
      Level,
      Regex,
    };

    Field field = Field::Text;
    bool negate = false;
    std::string value;
    int level = 0;
    std::optional<std::regex> regex;
  };

  struct Query
  {
    std::vector<Term> terms;
    // The words to mark in the messages
    std::vector<std::string> marks;
    bool badRegex = false;
  };

  Query parse(std::string_view text);

  bool matches(Query const& query, Record const& record);
}
