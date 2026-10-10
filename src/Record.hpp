#pragma once

#include <string>

namespace geode
{
  class Mod;
}

// Where a log came from: logs from the same mod and thread, one after another, form a group
struct Source
{
  geode::Mod* mod = nullptr;
  std::string name;
  std::string thread;

  bool operator==(Source const&) const = default;
};

// One log, already formatted for showing
struct Record
{
  Source source;
  int severity = 0;
  int nest = 0;
  int color = 245;
  std::string time;
  std::string content;
  // The content with numbers swapped for '#', only in the Similar folding mode
  std::string pattern;
};
