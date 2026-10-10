// Tests for src/Model.cpp: groups and the folding of repeats
#include "../src/Levels.hpp"
#include "../src/Model.hpp"
#include "../src/Text.hpp"
#include "Check.hpp"

#include <vector>

static settings::Config config(settings::Collapse collapse = settings::Collapse::Similar)
{
  settings::Config config;
  config.collapse = collapse;
  config.gapSeconds = 1.5;
  return config;
}

struct Log
{
  std::string content;
  int64_t millis = 0;
  std::string mod = "Mod";
  std::string thread = "Main";
  int severity = level::Info;
};

static void add(model::Model& model, settings::Config const& config, Log log)
{
  Record record{
      .source = {nullptr, log.mod, log.thread, ""},
      .severity = log.severity,
      .millis = log.millis,
      .content = log.content,
  };
  if (config.collapse == settings::Collapse::Similar)
    record.pattern = text::withoutNumbers(record.content);
  model.add(std::move(record), config);
}

// The rows of the last group: content and count
static std::vector<std::pair<std::string, int>> rows(model::Model const& model)
{
  std::vector<std::pair<std::string, int>> out;
  for (auto const& row : model.groups.back().rows)
    out.emplace_back(row.record.content, row.count);
  return out;
}

static void testIdentical()
{
  model::Model model;
  auto c = config(settings::Collapse::Identical);
  for (int i = 0; i < 5; i++)
    add(model, c, {"Same line"});
  CHECK(model.groups.size() == 1);
  CHECK(rows(model) == (std::vector<std::pair<std::string, int>>{{"Same line", 5}}));
  CHECK(model.groups.back().logs == 5);
  CHECK(model.logs == 5);

  // Identical mode doesn't fold lines that differ in numbers
  add(model, c, {"pos 1"});
  add(model, c, {"pos 2"});
  CHECK(rows(model).size() == 3);
}

static void testSimilar()
{
  model::Model model;
  auto c = config();
  for (int i = 0; i < 40; i++)
    add(model, c, {"pos x=" + std::to_string(i)});
  // One row, the latest values, the count
  CHECK(rows(model) == (std::vector<std::pair<std::string, int>>{{"pos x=39", 40}}));
}

static void testTwoLineBlocks()
{
  model::Model model;
  auto c = config();
  for (int i = 0; i < 3; i++)
  {
    add(model, c, {"Pre-loading icon " + std::to_string(i)});
    add(model, c, {"Finished icon " + std::to_string(i)});
  }
  // The block folds as a whole, the count on its last row
  CHECK(rows(model) == (std::vector<std::pair<std::string, int>>{{"Pre-loading icon 2", 1}, {"Finished icon 2", 3}}));
}

static void testBrokenBlockKeepsOrder()
{
  model::Model model;
  auto c = config();
  add(model, c, {"Alpha"});
  add(model, c, {"Beta"});
  add(model, c, {"Alpha"});
  add(model, c, {"Gamma"});
  // A round that doesn't finish stays as it was written
  CHECK(rows(model) == (std::vector<std::pair<std::string, int>>{{"Alpha", 1}, {"Beta", 1}, {"Alpha", 1}, {"Gamma", 1}}));
}

static void testBrokenBlockStartsAnother()
{
  model::Model model;
  auto c = config();
  add(model, c, {"A"});
  add(model, c, {"B"});
  add(model, c, {"A"});
  add(model, c, {"A"});
  // A B is broken by the second A, which then folds into the A before it
  CHECK(rows(model) == (std::vector<std::pair<std::string, int>>{{"A", 1}, {"B", 1}, {"A", 2}}));
}

static void testOff()
{
  model::Model model;
  auto c = config(settings::Collapse::Off);
  for (int i = 0; i < 3; i++)
    add(model, c, {"Same"});
  CHECK(rows(model).size() == 3);
}

static void testGroups()
{
  model::Model model;
  auto c = config();
  add(model, c, {"one", 0, "First"});
  add(model, c, {"two", 10, "First"});
  add(model, c, {"three", 20, "Second"});
  add(model, c, {"four", 30, "First"});
  // A new group each time the mod changes
  CHECK(model.groups.size() == 3);

  // A pause starts a new group of the same mod
  add(model, c, {"after a pause", 5000, "First"});
  CHECK(model.groups.size() == 4);
}

static void testThreads()
{
  model::Model model;
  auto c = config();
  add(model, c, {"Same", 0, "Mod", "Main"});
  add(model, c, {"Same", 1, "Mod", "Worker"});
  // One group for both threads, but lines of different threads don't fold together
  CHECK(model.groups.size() == 1);
  CHECK(rows(model).size() == 2);

  auto split = config();
  split.threadGroups = true;
  model::Model separate;
  add(separate, split, {"Same", 0, "Mod", "Main"});
  add(separate, split, {"Same", 1, "Mod", "Worker"});
  CHECK(separate.groups.size() == 2);
}

static void testMarkers()
{
  model::Model model;
  auto c = config();
  add(model, c, {"Same", 0});
  model.addMarker(Record{.kind = RecordKind::Marker, .source = {nullptr, "Marker", ""}, .content = "Mark 1"});
  add(model, c, {"Same", 1});
  // The marker sits alone, the same line after it doesn't fold into the one before
  CHECK(model.groups.size() == 3);
  CHECK(model.groups[1].marker);
  CHECK(model.markers == 1);
}

static void testCounts()
{
  model::Model model;
  auto c = config();
  add(model, c, {"a", 0, "Mod", "Main", level::Warning});
  add(model, c, {"b", 1, "Mod", "Main", level::Error});
  add(model, c, {"c", 2, "Other", "Main", level::Debug});
  CHECK(model.severityCounts[level::Warning - level::Trace] == 1);
  CHECK(model.severityCounts[level::Error - level::Trace] == 1);
  CHECK(model.mods.size() == 2);

  auto stats = model.modStats("Mod");
  CHECK(stats && stats->logs == 2 && stats->warnings == 1 && stats->errors == 1);
  CHECK(model.modStats("Missing") == nullptr);

  model.clear();
  CHECK(model.groups.empty() && model.logs == 0 && model.mods.empty());
}

static void testRate()
{
  model::ModStats stats;
  for (int i = 0; i < 50; i++)
    stats.count(100000 + i);
  // 50 logs in one second, over a window of 10 seconds
  CHECK(stats.rate(100500) == 5.0);
  // Long after, nothing
  CHECK(stats.rate(200000) == 0.0);
}

int main()
{
  testIdentical();
  testSimilar();
  testTwoLineBlocks();
  testBrokenBlockKeepsOrder();
  testBrokenBlockStartsAnother();
  testOff();
  testGroups();
  testThreads();
  testMarkers();
  testCounts();
  testRate();
  return finish("model");
}
