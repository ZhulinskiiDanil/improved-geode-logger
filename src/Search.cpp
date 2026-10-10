#include "Search.hpp"
#include "Levels.hpp"
#include "Text.hpp"

namespace search
{
  static std::vector<std::string> splitQuery(std::string_view text)
  {
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    for (char c : text)
    {
      if (c == '"')
        quoted = !quoted;
      else if (c == ' ' && !quoted)
      {
        if (!word.empty())
          words.push_back(std::move(word));
        word.clear();
      }
      else
        word.push_back(c);
    }
    if (!word.empty())
      words.push_back(std::move(word));
    return words;
  }

  static std::optional<int> levelNamed(std::string_view name)
  {
    auto is = [&](std::string_view known)
    { return name.size() == known.size() && text::containsCaseless(name, known); };

    if (is("trace"))
      return level::Trace;
    if (is("debug"))
      return level::Debug;
    if (is("info"))
      return level::Info;
    if (is("warn") || is("warning"))
      return level::Warning;
    if (is("error"))
      return level::Error;
    return std::nullopt;
  }

  Query parse(std::string_view text)
  {
    Query query;
    for (auto& word : splitQuery(text))
    {
      Term term;
      std::string_view view = word;
      if (view.size() > 1 && view[0] == '-')
      {
        term.negate = true;
        view.remove_prefix(1);
      }

      auto field = [&](std::string_view prefix)
      {
        if (view.size() <= prefix.size() || !text::containsCaseless(view.substr(0, prefix.size()), prefix))
          return false;
        view.remove_prefix(prefix.size());
        return true;
      };

      if (field("mod:"))
        term.field = Term::Field::Mod;
      else if (field("thread:"))
        term.field = Term::Field::Thread;
      else if (field("level:"))
      {
        auto level = levelNamed(view);
        if (!level)
          continue;
        term.field = Term::Field::Level;
        term.level = *level;
      }
      else if (field("re:"))
      {
        term.field = Term::Field::Regex;
        try
        {
          term.regex.emplace(std::string(view), std::regex::ECMAScript | std::regex::icase);
        }
        catch (std::regex_error const&)
        {
          query.badRegex = true;
          continue;
        }
      }

      term.value = std::string(view);
      if (term.field == Term::Field::Text && !term.negate)
        query.marks.push_back(term.value);
      query.terms.push_back(std::move(term));
    }
    return query;
  }

  static bool matches(Term const& term, Record const& record)
  {
    switch (term.field)
    {
    case Term::Field::Mod:
      return text::containsCaseless(record.source.name, term.value) || text::containsCaseless(record.source.modID, term.value);
    case Term::Field::Thread:
      return text::containsCaseless(record.source.thread, term.value);
    case Term::Field::Level:
      return record.severity >= term.level;
    case Term::Field::Regex:
      return std::regex_search(record.content, *term.regex);
    default:
      return text::containsCaseless(record.content, term.value) || text::containsCaseless(record.source.name, term.value);
    }
  }

  bool matches(Query const& query, Record const& record)
  {
    for (auto const& term : query.terms)
    {
      if (matches(term, record) == term.negate)
        return false;
    }
    return true;
  }
}
