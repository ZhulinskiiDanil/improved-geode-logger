#include "Model.hpp"

#include "Levels.hpp"

#include <algorithm>

namespace model
{
  static constexpr size_t MAX_CYCLE = 4;
  // Older groups go away after this many rows, the log file keeps everything
  static constexpr size_t MAX_ROWS = 50000;

  Model& get()
  {
    // Never destroyed: the log thread may still be adding while the game closes
    static auto* model = new Model();
    return *model;
  }

  static bool isRepeat(settings::Config const& config, Record const& printed, Record const& record)
  {
    if (printed.source != record.source || printed.severity != record.severity || printed.nest != record.nest)
      return false;
    if (config.collapse == settings::Collapse::Similar)
      return printed.pattern == record.pattern;
    return printed.content == record.content;
  }

  ModStats* Model::modStats(std::string const& name)
  {
    auto it = m_modIndex.find(name);
    return it == m_modIndex.end() ? nullptr : &mods[it->second];
  }

  void Model::endCycle()
  {
    if (m_cycle)
      m_floor = m_cycle->start + m_cycle->length;
    m_cycle.reset();
  }

  // The same folding as the console stream: a block of up to 4 rows that comes back
  // stays one block with the latest values and a counter
  bool Model::fold(settings::Config const& config, Group& group, Record& record)
  {
    auto& rows = group.rows;
    for (int attempt = 0; attempt < 2; attempt++)
    {
      if (!m_cycle)
      {
        auto available = rows.size() - std::min(m_floor, rows.size());
        for (size_t length = 1; length <= std::min(MAX_CYCLE, available); length++)
        {
          if (isRepeat(config, rows[rows.size() - length].record, record))
          {
            m_cycle = Cycle{.start = rows.size() - length, .length = length};
            break;
          }
        }
      }
      if (!m_cycle)
        return false;

      auto& cycle = *m_cycle;
      auto round = rows.size() - (cycle.start + cycle.length);
      if (isRepeat(config, rows[cycle.start + round].record, record))
      {
        if (round + 1 < cycle.length)
        {
          rows.push_back(Row{.record = std::move(record)});
          m_rows++;
          return true;
        }

        // The round is complete: its rows become the block's latest values
        for (size_t i = 0; i + 1 < cycle.length; i++)
          rows[cycle.start + i].record = std::move(rows[cycle.start + cycle.length + i].record);
        rows[cycle.start + cycle.length - 1].record = std::move(record);
        m_rows -= rows.size() - (cycle.start + cycle.length);
        rows.resize(cycle.start + cycle.length);
        cycle.count++;
        rows.back().count = cycle.count;
        return true;
      }

      endCycle();
    }
    return false;
  }

  void Model::add(Record record, settings::Config const& config)
  {
    // The logs' own times, so an old log file read back gets the same groups
    bool pause = config.gapSeconds > 0 && m_lastMillis &&
                 record.millis - *m_lastMillis >= static_cast<int64_t>(config.gapSeconds * 1000);
    m_lastMillis = record.millis;

    int severity = record.severity;
    severityCounts[std::clamp(severity - level::Trace, 0, 4)]++;
    logs++;

    auto [it, added] = m_modIndex.try_emplace(record.source.name, mods.size());
    if (added)
      mods.push_back(ModStats{.name = record.source.name, .mod = record.source.mod, .color = record.color});
    auto& stats = mods[it->second];
    stats.logs++;
    stats.count(record.millis);
    stats.warnings += severity == level::Warning;
    stats.errors += severity >= level::Error;

    bool sameGroup = !groups.empty() && !groups.back().marker && sameBlock(groups.back().source, record.source, config.threadGroups) && !pause;
    if (!sameGroup)
    {
      groups.push_back(Group{.id = m_nextGroup++, .source = record.source, .color = record.color, .firstTime = record.time, .startMillis = record.millis});
      m_cycle.reset();
      m_floor = 0;
    }

    auto& group = groups.back();
    group.logs++;
    group.warnings += severity == level::Warning;
    group.errors += severity >= level::Error;
    group.lastTime = record.time;

    bool folded = config.collapse != settings::Collapse::Off && fold(config, group, record);
    if (!folded)
    {
      if (config.collapse == settings::Collapse::Off)
        endCycle();
      group.rows.push_back(Row{.record = std::move(record)});
      m_rows++;
    }

    while (m_rows > MAX_ROWS && groups.size() > 1)
    {
      m_rows -= groups.front().rows.size();
      groups.pop_front();
    }

    revision.fetch_add(1, std::memory_order::relaxed);
  }

  void Model::addMarker(Record marker)
  {
    markers++;
    groups.push_back(Group{
        .id = m_nextGroup++,
        .source = marker.source,
        .color = marker.color,
        .firstTime = marker.time,
        .lastTime = marker.time,
        .startMillis = marker.millis,
        .marker = true,
    });
    groups.back().rows.push_back(Row{.record = std::move(marker)});
    m_rows++;
    m_cycle.reset();
    m_floor = 0;
    // The logs after a marker start a new group even without a pause
    m_lastMillis.reset();
    revision.fetch_add(1, std::memory_order::relaxed);
  }

  void Model::setWatch(Watch watch)
  {
    auto it = std::ranges::find_if(watches, [&](auto const& known)
                                   { return known.modID == watch.modID && known.name == watch.name; });
    if (it == watches.end())
    {
      watch.updates = 1;
      watches.push_back(std::move(watch));
    }
    else
    {
      it->value = std::move(watch.value);
      it->millis = watch.millis;
      it->updates++;
    }
    revision.fetch_add(1, std::memory_order::relaxed);
  }

  void Model::removeWatch(std::string const& modID, std::string const& name)
  {
    std::erase_if(watches, [&](auto const& watch)
                  { return watch.modID == modID && (name.empty() || watch.name == name); });
    revision.fetch_add(1, std::memory_order::relaxed);
  }

  void ModStats::count(int64_t millis)
  {
    auto second = millis / 1000;
    if (second > recentSecond)
    {
      // Seconds that went by without logs count as zero
      for (int64_t s = std::max(recentSecond + 1, second - RATE_SECONDS + 1); s <= second; s++)
        recent[s % RATE_SECONDS] = 0;
      recentSecond = second;
    }
    if (second > recentSecond - RATE_SECONDS)
      recent[second % RATE_SECONDS]++;
  }

  double ModStats::rate(int64_t nowMillis) const
  {
    auto now = nowMillis / 1000;
    int total = 0;
    for (int64_t s = now - RATE_SECONDS + 1; s <= now; s++)
    {
      if (s <= recentSecond && s > recentSecond - RATE_SECONDS)
        total += recent[s % RATE_SECONDS];
    }
    return total / static_cast<double>(RATE_SECONDS);
  }

  void Model::clear()
  {
    groups.clear();
    mods.clear();
    m_modIndex.clear();
    std::fill(std::begin(severityCounts), std::end(severityCounts), 0);
    logs = 0;
    markers = 0;
    m_rows = 0;
    m_lastMillis.reset();
    m_cycle.reset();
    m_floor = 0;
    revision.fetch_add(1, std::memory_order::relaxed);
  }
}
