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
    int64_t startMillis = 0;
    // A marker sits alone in its group, its only row is the marker
    bool marker = false;
  };

  // Seconds kept for the logs per second of a mod
  inline constexpr int RATE_SECONDS = 10;

  struct ModStats
  {
    std::string name;
    geode::Mod* mod = nullptr;
    int color = 245;
    int logs = 0;
    int warnings = 0;
    int errors = 0;

    // Logs in each of the last seconds, by second since the epoch
    int recent[RATE_SECONDS] = {};
    int64_t recentSecond = 0;

    void count(int64_t millis);
    // Logs per second over the last RATE_SECONDS seconds
    double rate(int64_t nowMillis) const;
  };

  // A value a mod pins in the console through the API, instead of logging it again and again
  struct Watch
  {
    std::string modID;
    std::string modName;
    int color = 245;
    std::string name;
    std::string value;
    int64_t millis = 0;
    int updates = 0;
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
    int markers = 0;
    std::vector<Watch> watches;

    void add(Record record, settings::Config const& config);
    void addMarker(Record marker);
    void setWatch(Watch watch);
    void removeWatch(std::string const& modID, std::string const& name);
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
    // Time of the last log, pauses are measured between the logs' own times
    std::optional<int64_t> m_lastMillis;
  };

  Model& get();
}
