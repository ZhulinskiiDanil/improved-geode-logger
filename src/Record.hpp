#pragma once

#include <cstdint>
#include <string>

namespace geode
{
  class Mod;
}

enum class RecordKind
{
  Log,
  // A line the developer put in to find what came after it
  Marker,
};

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
  RecordKind kind = RecordKind::Log;
  Source source;
  int severity = 0;
  int nest = 0;
  int color = 245;
  std::string time;
  // Milliseconds since the epoch, for the time between logs
  int64_t millis = 0;
  std::string content;
  // The content with numbers swapped for '#', only in the Similar folding mode
  std::string pattern;
};
