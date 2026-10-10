#pragma once

#include "Record.hpp"
#include "Settings.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Every log the interactive console can show, grouped and folded like the console stream
namespace model
{
  struct Row
  {
    Record record;
    // Shown on the last row of a repeating block: how many times the block came
    int count = 1;
  };

  struct Group
  {
    uint64_t id = 0;
    Source source;
    int color = 245;
    std::vector<Row> rows;
    bool collapsed = false;
    // Logs that came in, folded ones too
    int logs = 0;
    int warnings = 0;
    int errors = 0;
    std::string firstTime;
    std::string lastTime;
  };

  struct ModStats
  {
    std::string name;
    int color = 245;
    int logs = 0;
    int warnings = 0;
    int errors = 0;
    bool hidden = false;
  };

  struct Model
  {
    // Everything below is read and changed under this lock
    std::mutex mutex;
    std::deque<Group> groups;
    std::vector<ModStats> mods;
    // Logs by severity, Trace first
    int severityCounts[5] = {};
    uint64_t logs = 0;

    void add(Record record, settings::Config const& config);
    void clear();
    ModStats* modStats(std::string const& name);

    // Bumped on every change, the screen redraws when it moves
    std::atomic<uint64_t> revision = 0;

  private:
    struct Cycle
    {
      size_t start = 0;
      size_t length = 0;
      int count = 1;
    };

    bool fold(settings::Config const& config, Group& group, Record& record);
    void endCycle();

    std::optional<Cycle> m_cycle;
    // Rows of the last group before this one are not looked at for repeats anymore
    size_t m_floor = 0;
    uint64_t m_nextGroup = 1;
    size_t m_rows = 0;
    std::unordered_map<std::string, size_t> m_modIndex;
    std::optional<std::chrono::steady_clock::time_point> m_lastTime;
  };

  Model& get();
}
