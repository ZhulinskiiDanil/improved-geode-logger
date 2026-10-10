// Tests for src/Search.cpp
#include "../src/Levels.hpp"
#include "../src/Search.hpp"
#include "Check.hpp"

static Record log(std::string content, int severity = level::Info, std::string mod = "Icon Mayhem",
                  std::string thread = "Main", std::string modID = "zhulis.icon-mayhem")
{
  return Record{
      .source = {nullptr, std::move(mod), std::move(thread), std::move(modID)},
      .severity = severity,
      .content = std::move(content),
  };
}

static bool finds(std::string_view query, Record const& record)
{
  return search::matches(search::parse(query), record);
}

static void testWords()
{
  // An empty search lets everything through
  CHECK(finds("", log("anything")));
  CHECK(finds("hook", log("Enabled MenuLayer::init hook")));
  // Every word must be there, in any order and case
  CHECK(finds("INIT hook", log("Enabled MenuLayer::init hook")));
  CHECK(!finds("init missing", log("Enabled MenuLayer::init hook")));
  // The mod's name counts as text too
  CHECK(finds("mayhem", log("loaded")));
}

static void testNegationAndPhrases()
{
  CHECK(!finds("-hook", log("Enabled hook")));
  CHECK(finds("-hook", log("Enabled patch")));
  CHECK(finds("\"init hook\"", log("Enabled MenuLayer::init hook")));
  CHECK(!finds("\"hook init\"", log("Enabled MenuLayer::init hook")));
  // A lone minus is a word, not a negation of nothing
  CHECK(finds("-", log("a - b")));
}

static void testFields()
{
  CHECK(finds("mod:icon", log("x")));
  CHECK(finds("mod:zhulis.icon", log("x")));
  CHECK(!finds("mod:geode", log("x")));
  CHECK(finds("-mod:geode", log("x")));

  CHECK(finds("thread:main", log("x")));
  CHECK(!finds("thread:worker", log("x")));
  CHECK(finds("thread:worker", log("x", level::Info, "Icon Mayhem", "arc-worker-3")));

  CHECK(finds("level:warn", log("x", level::Warning)));
  CHECK(finds("level:warn", log("x", level::Error)));
  CHECK(!finds("level:warn", log("x", level::Info)));
  CHECK(finds("level:WARNING", log("x", level::Warning)));
  // An unknown level is left out of the search instead of hiding everything
  CHECK(finds("level:loud", log("x", level::Info)));
}

static void testRegex()
{
  CHECK(finds("re:bet+a", log("Searchable bettta warning")));
  CHECK(!finds("re:^beta", log("alpha beta")));
  CHECK(finds("re:^ALPHA", log("alpha beta")));

  auto bad = search::parse("re:(");
  CHECK(bad.badRegex);
  CHECK(bad.terms.empty());
}

static void testMarks()
{
  auto query = search::parse("alpha -beta mod:x \"two words\"");
  // Only the words to find in messages get marked
  CHECK(query.marks.size() == 2);
  CHECK(query.marks.size() == 2 && query.marks[0] == "alpha" && query.marks[1] == "two words");
}

int main()
{
  testWords();
  testNegationAndPhrases();
  testFields();
  testRegex();
  testMarks();
  return finish("search");
}
