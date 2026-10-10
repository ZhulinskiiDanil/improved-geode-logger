#include "Grouper.hpp"
#include "Console.hpp"
#include "Settings.hpp"
#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <fmt/chrono.h>
#include <mutex>

using namespace geode::prelude;

namespace grouper
{
  // Repeating blocks are looked for up to this many lines, like "Loading X" + "Loaded X"
  static constexpr size_t MAX_CYCLE = 4;
  static constexpr size_t MAX_ENTRIES = 16;
  // Taller blocks are printed again instead of folded
  static constexpr int MAX_FOLD_ROWS = 30;
  static constexpr int MAX_HEADER_WIDTH = 100;
  static constexpr int GUIDE_COLOR = 240;
  static constexpr int PLAIN_MOD_COLOR = 245;

  struct Source
  {
    Mod* mod = nullptr;
    std::string name;
    std::string thread;

    bool operator==(Source const&) const = default;
  };

  // One log as it is on the screen
  struct Entry
  {
    Source source;
    int severity = 0;
    int nest = 0;
    int color = PLAIN_MOD_COLOR;
    std::string time;
    std::string content;
    std::string pattern;
    std::vector<int> widths;
  };

  // A block of lines that keeps coming back. It sits at entries[start, start + length),
  // the lines after it are the next round, printed as they come until the round is complete
  struct Cycle
  {
    size_t start = 0;
    size_t length = 0;
    int count = 1;
  };

  static std::mutex s_mutex;
  // Lines printed under the current header, oldest first
  static std::vector<Entry> s_entries;
  static std::optional<Cycle> s_cycle;
  static std::optional<Source> s_block;
  static std::optional<std::chrono::steady_clock::time_point> s_lastTime;

  // ! --- Line building --- !

  // Collects one print, keeping the width of every line to know later how many rows it took
  class Lines
  {
  public:
    explicit Lines(bool colors) : m_colors(colors) {}

    void color(int fg)
    {
      if (m_colors)
        m_out += fmt::format("\x1b[38;5;{}m", fg);
    }

    void escape(std::string_view code)
    {
      if (m_colors)
        m_out += code;
    }

    void reset()
    {
      escape("\x1b[0m");
    }

    void text(std::string_view part)
    {
      m_out += part;
      m_width += text::width(part);
    }

    void end()
    {
      m_out += '\n';
      m_widths.push_back(m_width);
      m_width = 0;
    }

    // Widths of the lines from this one on, for an entry printed in the middle
    std::vector<int> widthsFrom(size_t line) const
    {
      return {m_widths.begin() + std::min(line, m_widths.size()), m_widths.end()};
    }

    int width() const { return m_width; }
    size_t lineCount() const { return m_widths.size(); }
    std::string const& str() const { return m_out; }

  private:
    bool m_colors;
    std::string m_out;
    int m_width = 0;
    std::vector<int> m_widths;
  };

  static int rowsFor(std::vector<int> const& widths, int columns)
  {
    int rows = 0;
    for (int width : widths)
      rows += std::max(1, (width + columns - 1) / columns);
    return rows;
  }

  // Geode's console colors: one for the time and level, one for the message
  static std::pair<int, int> severityColors(int severity)
  {
    switch (severity)
    {
    case Severity::Trace:
      return {54, 254};
    case Severity::Debug:
      return {243, 250};
    case Severity::Info:
      return {45, 254};
    case Severity::Warning:
      return {229, 230};
    case Severity::Error:
      return {9, 224};
    default:
      return {7, 7};
    }
  }

  static std::string_view severityName(int severity)
  {
    switch (severity)
    {
    case Severity::Trace:
      return "TRACE";
    case Severity::Debug:
      return "DEBUG";
    case Severity::Info:
      return "INFO ";
    case Severity::Warning:
      return "WARN ";
    case Severity::Error:
      return "ERROR";
    default:
      return "?????";
    }
  }

  static std::string timeOf(log::BorrowedLog const& log, bool millis)
  {
    auto local = asp::localtime(log.m_time.to_time_t());
    if (millis)
      return fmt::format("{:%H:%M:%S}.{:03}", local, log.m_time.timeSinceEpoch().millis() % 1000);
    return fmt::format("{:%H:%M:%S}", local);
  }

  static bool showsThread(std::string_view thread)
  {
    return !thread.empty() && thread != "Main";
  }

  // ! --- Printing --- !

  static void printHeader(Lines& out, Entry const& entry, int columns)
  {
    out.color(entry.color);
    out.escape("\x1b[1m");
    out.text("┌─ ");
    out.text(entry.source.name);
    out.reset();

    if (showsThread(entry.source.thread))
    {
      out.color(GUIDE_COLOR);
      out.text(" · ");
      out.text(entry.source.thread);
      out.reset();
    }

    out.color(entry.color);
    out.text(" ");
    auto fill = std::min(columns - 1, MAX_HEADER_WIDTH) - out.width();
    for (int i = 0; i < fill; i++)
      out.text("─");
    out.reset();
    out.end();
  }

  static void printEntry(Lines& out, settings::Config const& config, Entry const& entry, int count)
  {
    auto [prefixColor, messageColor] = severityColors(entry.severity);

    auto gutter = [&]
    {
      if (!config.groupByMod)
        return;
      out.color(entry.color);
      out.text("│ ");
      out.reset();
    };

    auto guides = [&]
    {
      if (entry.nest <= 0)
        return;
      out.color(GUIDE_COLOR);
      for (int i = 0; i < entry.nest; i++)
        out.text(config.nestGuides ? "│ " : "  ");
      out.reset();
    };

    gutter();

    out.color(prefixColor);
    out.text(entry.time);
    out.text(" ");
    out.text(severityName(entry.severity));
    out.reset();
    out.text(" ");

    if (!config.groupByMod)
    {
      if (!entry.source.thread.empty())
        out.text(fmt::format("[{}] ", entry.source.thread));
      out.color(entry.color);
      out.text(fmt::format("[{}]", entry.source.name));
      out.reset();
      out.text(": ");
    }

    // Lines after the first start under the message, not under the time
    auto indent = out.width() - (config.groupByMod ? 2 : 0);

    auto lines = text::splitLines(entry.content);
    for (size_t i = 0; i < lines.size(); i++)
    {
      if (i > 0)
      {
        out.end();
        gutter();
        out.text(std::string(indent, ' '));
      }

      guides();
      out.color(messageColor);
      out.text(lines[i]);
      out.reset();
    }

    if (count > 1)
    {
      out.text("  ");
      out.escape(fmt::format("\x1b[38;5;16m\x1b[48;5;{}m", config.groupByMod ? entry.color : 250));
      out.text(fmt::format(" ×{} ", count));
      out.reset();
    }

    out.end();
  }

  // ! --- Filter --- !

  static bool inList(std::vector<std::string> const& mods, log::BorrowedLog const& log)
  {
    if (mods.empty())
      return false;

    auto name = string::toLower(std::string(log.m_source));
    auto id = log.m_mod ? string::toLower(std::string(log.m_mod->getID())) : std::string();
    for (auto const& mod : mods)
    {
      if (mod == name || mod == id)
        return true;
    }
    return false;
  }

  static bool isShown(settings::Config const& config, log::BorrowedLog const& log)
  {
    int severity = log.m_severity;

    // The same rules Geode follows for its console
    if (severity < config.consoleLevel)
      return false;
    if (log.m_mod && (!log.m_mod->isLoggingEnabled() || severity < log.m_mod->getLogLevel()))
      return false;

    if (severity < Severity::Error && inList(config.mutedMods, log))
      return false;
    if (severity < Severity::Warning && !config.focusMods.empty() && !inList(config.focusMods, log))
      return false;
    return true;
  }

  // ! --- Folding --- !

  static bool isRepeat(settings::Config const& config, Entry const& printed, Entry const& entry)
  {
    if (printed.source != entry.source || printed.severity != entry.severity || printed.nest != entry.nest)
      return false;
    if (config.collapse == settings::Collapse::Similar)
      return printed.pattern == entry.pattern;
    return printed.content == entry.content;
  }

  // The shortest block at the end of the screen that starts like this line
  static std::optional<Cycle> findCycle(settings::Config const& config, Entry const& entry)
  {
    for (size_t length = 1; length <= std::min(MAX_CYCLE, s_entries.size()); length++)
    {
      if (isRepeat(config, s_entries[s_entries.size() - length], entry))
        return Cycle{.start = s_entries.size() - length, .length = length};
    }
    return std::nullopt;
  }

  static void endCycle()
  {
    // The folded block stays on the screen as it is, the lines after it can start a new one
    if (s_cycle)
      s_entries.erase(s_entries.begin(), s_entries.begin() + s_cycle->start + s_cycle->length);
    s_cycle.reset();
  }

  static void printLine(settings::Config const& config, Entry entry, Lines& out)
  {
    auto first = out.lineCount();
    printEntry(out, config, entry, 1);
    entry.widths = out.widthsFrom(first);
    s_entries.push_back(std::move(entry));
  }

  // The round is complete: the block and the round get erased and the block comes back
  // with the latest values and one more on its counter
  static bool foldRound(settings::Config const& config, Entry entry, int columns)
  {
    auto& cycle = *s_cycle;

    std::vector<int> erased;
    for (size_t i = cycle.start; i < s_entries.size(); i++)
      erased.insert(erased.end(), s_entries[i].widths.begin(), s_entries[i].widths.end());
    auto rows = rowsFor(erased, columns);

    // The new line takes as many rows as the one it replaces, give or take
    if (rows > std::min(MAX_FOLD_ROWS, console::windowRows() - 2))
      return false;

    std::vector<Entry> block(std::make_move_iterator(s_entries.begin() + cycle.start + cycle.length),
                             std::make_move_iterator(s_entries.end()));
    block.push_back(std::move(entry));
    s_entries.resize(cycle.start);
    cycle.count++;

    Lines out(true);
    for (size_t i = 0; i < block.size(); i++)
    {
      auto first = out.lineCount();
      printEntry(out, config, block[i], i + 1 == block.size() ? cycle.count : 1);
      block[i].widths = out.widthsFrom(first);
      s_entries.push_back(std::move(block[i]));
    }

    console::write(fmt::format("\x1b[{}F\x1b[J{}", rows, out.str()));
    return true;
  }

  // True when the line went into a repeating block
  static bool fold(settings::Config const& config, Entry const& entry, int columns)
  {
    // After a block ends, the lines printed after it may start the next one
    for (int attempt = 0; attempt < 2; attempt++)
    {
      if (!s_cycle)
        s_cycle = findCycle(config, entry);
      if (!s_cycle)
        return false;

      auto& cycle = *s_cycle;
      auto round = s_entries.size() - (cycle.start + cycle.length);
      if (isRepeat(config, s_entries[cycle.start + round], entry))
      {
        if (round + 1 < cycle.length)
        {
          Lines out(true);
          printLine(config, entry, out);
          console::write(out.str());
          return true;
        }
        if (foldRound(config, entry, columns))
          return true;
      }

      endCycle();
    }
    return false;
  }

  // ! --- Grouping --- !

  void handle(log::BorrowedLog const& log)
  {
    auto config = settings::get();
    if (!config->enabled || !console::isOpen() || !isShown(*config, log))
      return;

    auto nestSpaces = std::get<2>(log.truncateWithNest());

    Entry entry{
        .source = {log.m_mod, std::string(log.m_source), std::string(log.m_thread)},
        .severity = log.m_severity,
        .nest = nestSpaces > 0 ? (nestSpaces + 1) / 2 : 0,
        .color = config->modColors && log.m_mod ? text::colorFor(log.m_mod->getID()) : PLAIN_MOD_COLOR,
        .time = timeOf(log, config->millis),
        .content = std::string(log.m_content),
    };
    if (config->collapse == settings::Collapse::Similar)
      entry.pattern = text::withoutNumbers(entry.content);

    bool escapeCodes = console::hasEscapeCodes();
    auto now = std::chrono::steady_clock::now();

    std::lock_guard lock(s_mutex);
    int columns = console::columns();

    // Folding rewrites lines above the cursor, which needs escape codes
    if (escapeCodes && config->collapse != settings::Collapse::Off && fold(*config, entry, columns))
    {
      s_lastTime = now;
      return;
    }

    Lines out(escapeCodes);

    // A pause between bursts of logs: an empty line and the header again
    bool pause = config->gapSeconds > 0 && s_lastTime &&
                 now - *s_lastTime >= std::chrono::duration<double>(config->gapSeconds);
    if (pause)
      out.end();

    if (pause || s_block != entry.source)
    {
      if (config->groupByMod)
        printHeader(out, entry, columns);
      s_entries.clear();
      s_cycle.reset();
    }
    s_block = entry.source;

    // Folding was just turned off, a block left from before can't take lines anymore
    endCycle();
    if (s_entries.size() >= MAX_ENTRIES)
      s_entries.erase(s_entries.begin());

    printLine(*config, std::move(entry), out);
    console::write(out.str());
    s_lastTime = now;
  }
}
