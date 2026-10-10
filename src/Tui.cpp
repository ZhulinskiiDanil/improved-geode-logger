#include "Tui.hpp"
#include "Console.hpp"
#include "DevLoop.hpp"
#include "History.hpp"
#include "Look.hpp"
#include "Model.hpp"
#include "Search.hpp"
#include "Settings.hpp"
#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <thread>

#include <Windows.h>

using namespace geode::prelude;

namespace tui
{
  // ! --- Look --- !

  static constexpr int TITLE_BG = 25;
  static constexpr int TITLE_FG = 255;
  static constexpr int BAR_BG = 235;
  static constexpr int BAR_FG = 248;
  static constexpr int KEY_FG = 81;
  static constexpr int SELECT_BG = 238;
  static constexpr int DIM = 243;
  static constexpr int BORDER = 239;
  static constexpr int LIVE = 78;
  static constexpr int PAUSED = 221;
  static constexpr int WARNING_COLOR = 221;
  static constexpr int ERROR_COLOR = 203;
  static constexpr int MATCH_FG = 16;
  static constexpr int MATCH_BG = 221;
  static constexpr int SIDEBAR_WIDTH = 30;
  static constexpr auto FRAME_TIME = std::chrono::milliseconds(33);
  static constexpr int BANNER_REBUILT = 0;
  // Logs a second from which a mod counts as noisy in the mods list
  static constexpr double NOISY_RATE = 5.0;
  static constexpr int BANNER_CRASH = 1;

  struct Style
  {
    int fg = -1;
    int bg = -1;
    bool bold = false;
  };

  // One screen row being drawn, cut to its width
  class Cells
  {
  public:
    explicit Cells(int width, int bg = -1) : m_width(width), m_bg(bg) {}

    int left() const { return m_width - m_used; }
    int used() const { return m_used; }

    // Text that doesn't fit ends with "…", keeping `reserve` columns free at the end
    void put(std::string_view text, Style style = {}, int reserve = 0)
    {
      int room = left() - reserve;
      if (room <= 0 || text.empty())
        return;

      auto part = text::fit(text, room);
      bool cut = part.size() < text.size();
      if (cut)
        part = text::fit(text, room - 1);

      sgr(style);
      m_out += part;
      m_used += text::width(part);
      if (cut)
      {
        m_out += "…";
        m_used++;
      }
    }

    void pad(int until, Style style = {})
    {
      until = std::min(until, m_width);
      if (until <= m_used)
        return;
      sgr(style);
      m_out.append(until - m_used, ' ');
      m_used = until;
    }

    // The rest of the row goes to the right side, as far right as it fits
    void right(std::string_view text, Style style = {})
    {
      pad(m_width - text::width(text), {});
      put(text, style);
    }

    std::string finish()
    {
      pad(m_width);
      return m_out + "\x1b[0m";
    }

  private:
    void sgr(Style style)
    {
      if (style.bg < 0)
        style.bg = m_bg;
      m_out += "\x1b[0m";
      if (style.fg >= 0)
        m_out += fmt::format("\x1b[38;5;{}m", style.fg);
      if (style.bg >= 0)
        m_out += fmt::format("\x1b[48;5;{}m", style.bg);
      if (style.bold)
        m_out += "\x1b[1m";
    }

    int m_width;
    int m_bg;
    int m_used = 0;
    std::string m_out;
  };

  static std::string compact(uint64_t number)
  {
    if (number < 1000)
      return std::to_string(number);
    if (number < 10000)
      return fmt::format("{:.1f}k", number / 1000.0);
    if (number < 1000000)
      return fmt::format("{}k", number / 1000);
    return fmt::format("{:.1f}M", number / 1000000.0);
  }

  static std::string_view levelName(int severity)
  {
    switch (severity)
    {
    case Severity::Trace:
      return "TRACE";
    case Severity::Debug:
      return "DEBUG";
    case Severity::Info:
      return "INFO";
    case Severity::Warning:
      return "WARN";
    default:
      return "ERROR";
    }
  }

  // ! --- State --- !

  // A line of the list: a group's header (row -1) or one of its rows
  struct LineRef
  {
    uint64_t group = 0;
    int row = -1;

    bool operator==(LineRef const&) const = default;
  };

  struct Line
  {
    LineRef ref;
    model::Group* group = nullptr;
    model::Row* row = nullptr;
  };

  // Where things were drawn on the last frame, for the keys and the mouse
  struct Layout
  {
    int height = 0;
    int width = 0;
    int listRows = 0;
    int sidebarWidth = 0;
    int top = 0;
    int sidebarTop = 0;
    // Screen row of the list's first line
    int listTop = 1;
    // What each banner under the title does when clicked
    std::vector<int> banners;
  };

  enum class TimeMode
  {
    Clock,
    // Since the log before it
    SincePrevious,
    // Since the first log of its group
    SinceGroup,
  };

  // What the details panel says about a mod, gathered on the main thread where hooks live
  struct ModCard
  {
    std::string name;
    std::vector<std::string> lines;
  };

  struct State
  {
    bool follow = true;
    std::optional<LineRef> selected;
    std::optional<LineRef> top;
    int selectedIndex = -1;
    int topIndex = 0;

    int minLevel = Severity::Trace;
    std::string search;
    bool typing = false;
    // Earlier searches, newest last, and where Up and Down are in them while typing
    std::vector<std::string> searches;
    int searchIndex = -1;

    bool sidebar = false;
    bool sidebarFocus = false;
    int sidebarIndex = 0;

    bool details = false;
    bool help = false;
    // The details panel shows a mod's card or a text (the last crash) instead of the selected log
    std::optional<std::string> detailMod;
    std::optional<std::pair<std::string, std::string>> detailText;
    int detailScroll = 0;

    TimeMode timeMode = TimeMode::Clock;

    // Mods whose logs are hidden here, by name, kept between launches
    std::set<std::string> hiddenMods;
    bool watchPanel = true;

    // An older log file instead of the live logs
    std::optional<std::string> historyName;
    std::optional<std::vector<std::filesystem::path>> picker;
    int pickerIndex = 0;

    uint64_t logsAtPause = 0;
    int errorsAtPause = 0;
    std::string flash;
    std::chrono::steady_clock::time_point flashUntil;
  };

  static std::atomic<bool> s_wanted = false;
  static std::atomic<bool> s_started = false;

  // Only the screen's thread touches these
  static bool s_active = false;
  static bool s_dirty = true;
  static HANDLE s_input = INVALID_HANDLE_VALUE;
  static DWORD s_inputMode = 0;
  static DWORD s_buttons = 0;
  static State s;
  static Layout s_layout;
  static std::vector<LineRef> s_view;
  static std::vector<std::string> s_sidebarMods;

  // The logs being looked at: the live ones, or an older file opened with L
  static std::unique_ptr<model::Model> s_history;

  static model::Model& viewModel()
  {
    return s_history ? *s_history : model::get();
  }

  // Set from other threads when the screen should be drawn again
  static std::atomic<bool> s_wake = false;
  static std::mutex s_cardMutex;
  static std::optional<ModCard> s_card;

  static void flash(std::string text)
  {
    s.flash = std::move(text);
    s.flashUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  }

  // ! --- List --- !

  // ! --- Search --- !

  using search::Query;

  static Query const& query()
  {
    static Query cached;
    static std::string cachedText;
    if (cachedText != s.search)
    {
      cached = search::parse(s.search);
      cachedText = s.search;
    }
    return cached;
  }

  static bool passes(Record const& record)
  {
    return record.severity >= s.minLevel && search::matches(query(), record);
  }

  static bool filtering()
  {
    return !s.search.empty() || s.minLevel > Severity::Trace;
  }

  static std::vector<Line> flatten(model::Model& model)
  {
    std::vector<Line> lines;
    for (auto& group : model.groups)
    {
      if (s.hiddenMods.contains(group.source.name))
        continue;

      auto start = lines.size();
      lines.push_back({{group.id, -1}, &group, nullptr});

      // Markers stay whatever the filters are, they are what you look for
      if (group.marker)
        continue;

      bool any = false;
      for (size_t i = 0; i < group.rows.size(); i++)
      {
        auto& row = group.rows[i];
        if (!passes(row.record))
          continue;
        any = true;
        if (group.collapsed)
          break;
        lines.push_back({{group.id, static_cast<int>(i)}, &group, &row});
      }

      // A search or level filter hides groups with nothing left in them
      if (!any && filtering())
        lines.resize(start);
    }
    return lines;
  }

  static int indexOf(std::vector<Line> const& lines, std::optional<LineRef> const& ref)
  {
    if (!ref)
      return -1;
    int header = -1;
    for (int i = static_cast<int>(lines.size()) - 1; i >= 0; i--)
    {
      if (lines[i].ref == *ref)
        return i;
      if (lines[i].ref.group == ref->group && lines[i].ref.row == -1)
        header = i;
    }
    // The row was folded away or filtered out, its group's header stands in
    return header;
  }

  static model::Group* findGroup(model::Model& model, uint64_t id)
  {
    for (auto it = model.groups.rbegin(); it != model.groups.rend(); ++it)
    {
      if (it->id == id)
        return &*it;
    }
    return nullptr;
  }

  // ! --- Moving around --- !

  static void pause()
  {
    if (!s.follow)
      return;
    s.follow = false;
    s.logsAtPause = viewModel().logs;
    s.errorsAtPause = viewModel().severityCounts[Severity::Error - Severity::Trace];
    if (!s_view.empty())
    {
      s.selected = s_view.back();
      s.selectedIndex = static_cast<int>(s_view.size()) - 1;
    }
  }

  static void follow()
  {
    s.follow = true;
    s.selected.reset();
  }

  static void select(int index)
  {
    if (s_view.empty())
      return;
    index = std::clamp(index, 0, static_cast<int>(s_view.size()) - 1);
    s.selected = s_view[index];
    s.selectedIndex = index;
  }

  static void move(int delta)
  {
    if (s_view.empty())
      return;
    if (s.follow)
    {
      if (delta >= 0)
        return;
      pause();
      delta++;
    }

    int index = s.selectedIndex + delta;
    // Going down past the last line goes back to following the new logs
    if (index >= static_cast<int>(s_view.size()))
    {
      follow();
      return;
    }
    select(index);
  }

  static void scroll(int delta)
  {
    if (s_view.empty())
      return;
    if (s.follow && delta >= 0)
      return;
    pause();

    int last = std::max(0, static_cast<int>(s_view.size()) - s_layout.listRows);
    int top = std::clamp(s_layout.top + delta, 0, last);
    if (delta > 0 && s_layout.top + delta > last)
    {
      follow();
      return;
    }
    s.top = s_view[top];
    // The selection stays on the screen
    select(std::clamp(s.selectedIndex, top, top + s_layout.listRows - 1));
  }

  static std::optional<Line> selectedLine(std::vector<Line> const& lines)
  {
    if (s.follow)
      return lines.empty() ? std::nullopt : std::optional(lines.back());
    auto index = indexOf(lines, s.selected);
    return index < 0 ? std::nullopt : std::optional(lines[index]);
  }

  // ! --- Actions --- !

  static void setCollapsed(LineRef ref, std::optional<bool> collapsed)
  {
    auto& model = viewModel();
    std::lock_guard lock(model.mutex);
    if (auto group = findGroup(model, ref.group))
    {
      group->collapsed = collapsed.value_or(!group->collapsed);
      if (group->collapsed && !s.follow)
        s.selected = LineRef{group->id, -1};
    }
  }

  static void setAllCollapsed(bool collapsed)
  {
    auto& model = viewModel();
    std::lock_guard lock(model.mutex);
    for (auto& group : model.groups)
      group.collapsed = collapsed;
    if (s.selected)
      s.selected->row = -1;
    flash(collapsed ? "Folded every group" : "Unfolded every group");
  }

  static std::string formatRow(model::Group const& group, model::Row const& row)
  {
    auto line = fmt::format("{} {} [{}]: {}", row.record.time, levelName(row.record.severity), group.source.name, row.record.content);
    if (row.count > 1)
      line += fmt::format(" (x{})", row.count);
    return line;
  }

  static void copyToClipboard(std::string const& utf8)
  {
    auto wide = string::utf8ToWide(utf8);
    if (!OpenClipboard(nullptr))
      return;
    EmptyClipboard();
    if (auto memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t)))
    {
      std::memcpy(GlobalLock(memory), wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory))
        GlobalFree(memory);
    }
    CloseClipboard();
  }

  static void closeDetails();

  // Markdown wraps it in a code block with the mod's name, ready for a bug report
  static void copySelection(bool markdown = false)
  {
    if (s.details && s.detailText)
    {
      copyToClipboard(markdown ? fmt::format("```\n{}\n```", s.detailText->second) : s.detailText->second);
      flash("Copied the whole text");
      return;
    }

    auto& model = viewModel();
    std::string text;
    std::string heading;
    int lines = 0;
    {
      std::lock_guard lock(model.mutex);
      auto line = selectedLine(flatten(model));
      if (!line)
        return;

      auto const& group = *line->group;
      auto modID = group.source.mod ? std::string(group.source.mod->getID()) : std::string();
      if (line->row)
      {
        text = formatRow(group, *line->row) + "\n";
        lines = 1;
      }
      else
      {
        for (auto const& row : group.rows)
        {
          if (!passes(row.record))
            continue;
          text += formatRow(group, row) + "\n";
          lines++;
        }
      }
      heading = modID.empty() ? fmt::format("**{}**", group.source.name) : fmt::format("**{}** (`{}`)", group.source.name, modID);
      heading += fmt::format(" · {} {}", lines, lines == 1 ? "log" : "logs");
    }

    if (markdown)
      text = fmt::format("{}\n```\n{}```", heading, text);
    else if (!text.empty())
      text.pop_back();
    copyToClipboard(text);
    flash(fmt::format("Copied {}{}", lines == 1 ? std::string("the log") : fmt::format("{} logs", lines), markdown ? " as Markdown" : ""));
  }

  // Every warning and error of the session as Markdown, grouped by mod, for a bug report
  static void copyProblems()
  {
    auto& model = viewModel();
    std::map<std::string, std::vector<std::string>> byMod;
    int problems = 0;
    {
      std::lock_guard lock(model.mutex);
      for (auto const& group : model.groups)
      {
        if (group.marker)
          continue;
        for (auto const& row : group.rows)
        {
          if (row.record.severity < Severity::Warning)
            continue;
          byMod[group.source.name].push_back(formatRow(group, row));
          problems++;
        }
      }
    }

    if (problems == 0)
    {
      flash("No warnings or errors to copy");
      return;
    }

    std::string text;
    for (auto const& [mod, lines] : byMod)
    {
      text += fmt::format("**{}** · {} {}\n```\n", mod, lines.size(), lines.size() == 1 ? "problem" : "problems");
      for (auto const& line : lines)
        text += line + "\n";
      text += "```\n";
    }
    copyToClipboard(text);
    flash(fmt::format("Copied {} warnings and errors from {} mods as Markdown", problems, byMod.size()));
  }

  static std::filesystem::path exportsDir()
  {
    return Mod::get()->getSaveDir() / "exports";
  }

  // Everything the filters let through, folded groups too, in Geode's own format
  static void exportView()
  {
    auto& model = viewModel();
    std::string text;
    int logs = 0;
    {
      std::lock_guard lock(model.mutex);
      for (auto const& group : model.groups)
      {
        if (s.hiddenMods.contains(group.source.name))
          continue;
        if (group.marker)
        {
          text += fmt::format("──── {} · {} ────\n", group.rows.front().record.content, group.rows.front().record.time);
          continue;
        }
        for (auto const& row : group.rows)
        {
          if (!passes(row.record))
            continue;
          text += formatRow(group, row) + "\n";
          logs++;
        }
      }
    }

    std::error_code error;
    std::filesystem::create_directories(exportsDir(), error);
    auto now = asp::localtime(asp::SystemTime::now().to_time_t());
    auto path = exportsDir() / fmt::format("{} {:%Y-%m-%d %H.%M.%S}.log", s.historyName ? "old log" : "logs", now);
    if (auto result = file::writeString(path, text); !result)
    {
      flash(fmt::format("Can't save: {}", result.unwrapErr()));
      return;
    }
    flash(fmt::format("Saved {} logs to {} · W opens the folder", logs, path.filename().string()));
  }

  // ! --- Older logs --- !

  static void openPicker()
  {
    s.picker = history::files();
    s.pickerIndex = 0;
    if (s.picker->empty())
    {
      s.picker.reset();
      flash("No older log files");
    }
  }

  static void openHistory(std::filesystem::path const& path)
  {
    auto config = settings::get();
    s_history = history::load(path, *config);
    s.historyName = path.filename().string();
    s.picker.reset();
    closeDetails();
    s.follow = false;
    s.selected.reset();
    s.top.reset();
    s.selectedIndex = 0;
    s_layout.top = 0;
    flash(fmt::format("{} logs from {}", s_history->logs, *s.historyName));
  }

  static void closeHistory()
  {
    s_history.reset();
    s.historyName.reset();
    closeDetails();
    s.selected.reset();
    s.top.reset();
    s.follow = true;
  }

  static void onPickerKey(KEY_EVENT_RECORD const& key, wchar_t c)
  {
    int count = static_cast<int>(s.picker->size());
    int page = std::max(1, s_layout.listRows - 2);
    switch (key.wVirtualKeyCode)
    {
    case VK_UP:
      s.pickerIndex = std::max(0, s.pickerIndex - 1);
      return;
    case VK_DOWN:
      s.pickerIndex = std::min(count - 1, s.pickerIndex + 1);
      return;
    case VK_PRIOR:
      s.pickerIndex = std::max(0, s.pickerIndex - page);
      return;
    case VK_NEXT:
      s.pickerIndex = std::min(count - 1, s.pickerIndex + page);
      return;
    case VK_RETURN:
      openHistory((*s.picker)[s.pickerIndex]);
      return;
    case VK_ESCAPE:
      s.picker.reset();
      return;
    default:
      break;
    }
    if (c == L'k')
      s.pickerIndex = std::max(0, s.pickerIndex - 1);
    else if (c == L'j')
      s.pickerIndex = std::min(count - 1, s.pickerIndex + 1);
    else if (c == L'L' || c == L'q')
      s.picker.reset();
  }

  // ! --- Saved between launches --- !

  static void loadState()
  {
    auto mod = Mod::get();
    s.minLevel = std::clamp(mod->getSavedValue<int>("ui-level", Severity::Trace), static_cast<int>(Severity::Trace), static_cast<int>(Severity::Error));
    s.timeMode = static_cast<TimeMode>(std::clamp(mod->getSavedValue<int>("ui-time", 0), 0, 2));
    s.sidebar = mod->getSavedValue<bool>("ui-sidebar", false);
    s.watchPanel = mod->getSavedValue<bool>("ui-watches", true);
    for (auto const& name : mod->getSavedValue<std::vector<std::string>>("ui-hidden", {}))
      s.hiddenMods.insert(name);
    s.searches = mod->getSavedValue<std::vector<std::string>>("ui-searches", {});
  }

  // Saved values belong to the main thread
  static void saveState()
  {
    Loader::get()->queueInMainThread([level = s.minLevel, time = static_cast<int>(s.timeMode), sidebar = s.sidebar,
                                      watches = s.watchPanel, hidden = std::vector<std::string>(s.hiddenMods.begin(), s.hiddenMods.end()),
                                      searches = s.searches]
                                     {
      auto mod = Mod::get();
      mod->setSavedValue("ui-level", level);
      mod->setSavedValue("ui-time", time);
      mod->setSavedValue("ui-sidebar", sidebar);
      mod->setSavedValue("ui-watches", watches);
      mod->setSavedValue("ui-hidden", hidden);
      mod->setSavedValue("ui-searches", searches); });
  }

  static void toggleMod(int index)
  {
    if (index < 0 || index >= static_cast<int>(s_sidebarMods.size()))
      return;
    auto const& name = s_sidebarMods[index];
    if (!s.hiddenMods.erase(name))
      s.hiddenMods.insert(name);
    saveState();
  }

  // Only this mod, or everyone again when it already was the only one
  static void soloMod(int index)
  {
    if (index < 0 || index >= static_cast<int>(s_sidebarMods.size()))
      return;
    auto name = s_sidebarMods[index];
    bool alone = !s.hiddenMods.contains(name) && std::ranges::all_of(s_sidebarMods, [&](auto const& other)
                                                                     { return other == name || s.hiddenMods.contains(other); });
    s.hiddenMods.clear();
    if (!alone)
    {
      for (auto const& other : s_sidebarMods)
      {
        if (other != name)
          s.hiddenMods.insert(other);
      }
    }
    saveState();
    flash(alone ? "Showing every mod" : fmt::format("Showing only {}", name));
  }

  static void showAllMods()
  {
    s.hiddenMods.clear();
    saveState();
    flash("Showing every mod");
  }

  static void clearLogs()
  {
    auto& model = viewModel();
    std::lock_guard lock(model.mutex);
    model.clear();
    follow();
    s.top.reset();
    flash("Cleared, the log file still has everything");
  }

  // ! --- Developing --- !

  // The next line down (or up) that is a warning, an error or a marker, or the next group
  static void jump(int direction, bool groups)
  {
    auto& model = viewModel();
    std::lock_guard lock(model.mutex);
    auto lines = flatten(model);
    int count = static_cast<int>(lines.size());
    if (count == 0)
      return;

    int from = s.follow ? count : indexOf(lines, s.selected);
    if (from < 0)
      from = std::clamp(s.selectedIndex, 0, count - 1);

    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
      auto const& line = lines[i];
      bool hit = groups ? line.row == nullptr
                        : line.group->marker || (line.row && line.row->record.severity >= Severity::Warning);
      if (!hit)
        continue;

      if (s.follow)
      {
        s.follow = false;
        s.logsAtPause = model.logs;
      }
      s.selected = line.ref;
      s.selectedIndex = i;
      return;
    }
    flash(direction > 0 ? "Nothing more below" : "Nothing more above");
  }

  static std::string_view timeModeName(TimeMode mode)
  {
    switch (mode)
    {
    case TimeMode::SincePrevious:
      return "since the log before";
    case TimeMode::SinceGroup:
      return "since the group's start";
    default:
      return "clock";
    }
  }

  static void nextTimeMode()
  {
    s.timeMode = s.timeMode == TimeMode::Clock           ? TimeMode::SincePrevious
                 : s.timeMode == TimeMode::SincePrevious ? TimeMode::SinceGroup
                                                         : TimeMode::Clock;
    flash(fmt::format("Time: {}", timeModeName(s.timeMode)));
    saveState();
  }

  static std::optional<std::string> sidebarModName()
  {
    if (s.sidebarIndex < 0 || s.sidebarIndex >= static_cast<int>(s_sidebarMods.size()))
      return std::nullopt;
    return s_sidebarMods[s.sidebarIndex];
  }

  static Mod* modNamed(std::string const& name)
  {
    auto& model = viewModel();
    std::lock_guard lock(model.mutex);
    auto stats = model.modStats(name);
    return stats ? stats->mod : nullptr;
  }

  // Version and hooks come from the main thread, the card fills in once it answers
  static void requestCard(std::string const& name)
  {
    auto mod = modNamed(name);
    {
      std::lock_guard lock(s_cardMutex);
      s_card = ModCard{name, {"  Loading…"}};
    }

    Loader::get()->queueInMainThread([name, mod]
                                     {
      std::vector<std::string> lines;
      if (!mod)
        lines.push_back("  Not a mod: these lines come from the game's own output");
      else
      {
        std::string developers;
        for (auto const& developer : mod->getDevelopers())
          developers += (developers.empty() ? "" : ", ") + developer;

        lines.push_back(fmt::format("  {} {} by {}", mod->getID(), mod->getVersion().toVString(), developers));
        lines.push_back(fmt::format("  Logs {} from {} up · in the mods list: + more, - fewer, l on/off",
                                    mod->isLoggingEnabled() ? "on" : "off", levelName(mod->getLogLevel())));

        auto hooks = mod->getHooks();
        lines.push_back(fmt::format("  {} hooks, {} patches", hooks.size(), mod->getPatches().size()));
        for (auto hook : hooks)
          lines.push_back(fmt::format("    {} {}", hook->isEnabled() ? "●" : "○", hook->getDisplayName()));
      }

      {
        std::lock_guard lock(s_cardMutex);
        if (s_card && s_card->name == name)
          s_card->lines = std::move(lines);
      }
      s_wake = true; });
  }

  static void openCard()
  {
    auto name = sidebarModName();
    if (!name)
      return;
    s.detailMod = *name;
    s.detailText.reset();
    s.details = true;
    s.detailScroll = 0;
    requestCard(*name);
  }

  // A mod's own level and logging, kept between launches: Geode forgets them on a restart
  static void saveModLevel(Mod* mod)
  {
    Loader::get()->queueInMainThread([mod]
                                     {
      auto saved = Mod::get()->getSavedValue<matjson::Value>("mod-levels", matjson::Value::object());
      if (!saved.isObject())
        saved = matjson::Value::object();
      std::string id(mod->getID());
      // Back to Geode's own: everything, logging on
      if (mod->getLogLevel() == Severity::Trace && mod->isLoggingEnabled())
        saved.erase(id);
      else
        saved[id] = matjson::makeObject({{"level", static_cast<int>(mod->getLogLevel())}, {"logging", mod->isLoggingEnabled()}});
      Mod::get()->setSavedValue("mod-levels", saved); });
  }

  // Before the other mods load, so their own loading logs follow the levels too
  static void applyModLevels()
  {
    auto saved = Mod::get()->getSavedValue<matjson::Value>("mod-levels", matjson::Value::object());
    if (!saved.isObject())
      return;
    for (auto const& [id, value] : saved)
    {
      auto mod = Loader::get()->getInstalledMod(id);
      if (!mod)
        continue;
      auto level = std::clamp(value["level"].asInt().unwrapOr(Severity::Trace), static_cast<int64_t>(Severity::Trace), static_cast<int64_t>(Severity::Error));
      mod->setLogLevel(Severity::cast(static_cast<int>(level)));
      mod->setLoggingEnabled(value["logging"].asBool().unwrapOr(true));
    }
  }

  static void resetModLevel()
  {
    auto name = sidebarModName();
    auto mod = name ? modNamed(*name) : nullptr;
    if (!mod)
      return;
    mod->setLogLevel(Severity::Trace);
    mod->setLoggingEnabled(true);
    saveModLevel(mod);
    flash(fmt::format("{} logs everything again", *name));
    if (s.detailMod == name)
      requestCard(*name);
  }

  static void changeModLevel(int delta)
  {
    auto name = sidebarModName();
    auto mod = name ? modNamed(*name) : nullptr;
    if (!mod)
    {
      flash("Not a mod, its level can't change");
      return;
    }

    int level = std::clamp(static_cast<int>(mod->getLogLevel()) + delta, static_cast<int>(Severity::Trace), static_cast<int>(Severity::Error));
    mod->setLogLevel(Severity::cast(level));
    saveModLevel(mod);
    flash(fmt::format("{} logs from {} up", *name, levelName(level)));
    if (s.detailMod == name)
      requestCard(*name);
  }

  static void toggleModLogging()
  {
    auto name = sidebarModName();
    auto mod = name ? modNamed(*name) : nullptr;
    if (!mod)
    {
      flash("Not a mod, its logs can't be turned off");
      return;
    }

    mod->setLoggingEnabled(!mod->isLoggingEnabled());
    saveModLevel(mod);
    flash(fmt::format("{} logs {}", *name, mod->isLoggingEnabled() ? "on" : "off, in the log file too"));
    if (s.detailMod == name)
      requestCard(*name);
  }

  static void openCrash()
  {
    auto crash = devloop::lastCrash();
    if (!crash)
    {
      flash("The last session didn't crash");
      return;
    }
    s.detailText = std::pair(fmt::format(" Crash at {} · {} ", crash->time, crash->path), devloop::readCrash());
    s.detailMod.reset();
    s.details = true;
    s.detailScroll = 0;
    devloop::dismissCrash();
  }

  static void closeDetails()
  {
    s.details = false;
    s.detailMod.reset();
    s.detailText.reset();
    s.detailScroll = 0;
  }

  // ! --- Keys --- !

  static void rememberSearch()
  {
    if (s.search.empty())
      return;
    std::erase(s.searches, s.search);
    s.searches.push_back(s.search);
    if (s.searches.size() > 20)
      s.searches.erase(s.searches.begin());
    saveState();
  }

  static void onTyping(KEY_EVENT_RECORD const& key)
  {
    int count = static_cast<int>(s.searches.size());
    switch (key.wVirtualKeyCode)
    {
    case VK_RETURN:
      s.typing = false;
      rememberSearch();
      s.searchIndex = -1;
      return;
    // Up and Down go through the earlier searches
    case VK_UP:
      if (count == 0)
        return;
      s.searchIndex = s.searchIndex < 0 ? count - 1 : std::max(0, s.searchIndex - 1);
      s.search = s.searches[s.searchIndex];
      return;
    case VK_DOWN:
      if (s.searchIndex < 0)
        return;
      if (++s.searchIndex >= count)
      {
        s.searchIndex = -1;
        s.search.clear();
      }
      else
        s.search = s.searches[s.searchIndex];
      return;
    case VK_ESCAPE:
      s.typing = false;
      s.search.clear();
      s.searchIndex = -1;
      return;
    case VK_BACK:
      // Drop the last UTF-8 character
      while (!s.search.empty() && (static_cast<uint8_t>(s.search.back()) & 0xC0) == 0x80)
        s.search.pop_back();
      if (!s.search.empty())
        s.search.pop_back();
      return;
    default:
      break;
    }

    wchar_t c = key.uChar.UnicodeChar;
    if (c >= 0x20)
    {
      std::wstring wide(1, c);
      s.search += string::wideToUtf8(wide);
    }
  }

  static void onSidebarKey(KEY_EVENT_RECORD const& key, wchar_t c)
  {
    int count = static_cast<int>(s_sidebarMods.size());
    int before = s.sidebarIndex;
    // An open card follows the selected mod
    auto moved = [&]
    {
      if (s.detailMod && s.sidebarIndex != before)
        openCard();
    };

    switch (key.wVirtualKeyCode)
    {
    case VK_UP:
      s.sidebarIndex = std::max(0, s.sidebarIndex - 1);
      moved();
      return;
    case VK_DOWN:
      s.sidebarIndex = std::min(count - 1, s.sidebarIndex + 1);
      moved();
      return;
    case VK_PRIOR:
      s.sidebarIndex = std::max(0, s.sidebarIndex - s_layout.listRows);
      moved();
      return;
    case VK_NEXT:
      s.sidebarIndex = std::min(count - 1, s.sidebarIndex + s_layout.listRows);
      moved();
      return;
    case VK_SPACE:
      toggleMod(s.sidebarIndex);
      return;
    case VK_RETURN:
      if (s.detailMod)
        closeDetails();
      else
        openCard();
      return;
    case VK_ESCAPE:
      if (s.detailMod)
      {
        closeDetails();
        return;
      }
      s.sidebarFocus = false;
      return;
    case VK_TAB:
    case VK_RIGHT:
      if (s.detailMod)
        closeDetails();
      s.sidebarFocus = false;
      return;
    default:
      break;
    }

    if (c == L's')
      soloMod(s.sidebarIndex);
    else if (c == L'a')
      showAllMods();
    else if (c == L'+' || c == L'=')
      changeModLevel(-1);
    else if (c == L'-')
      changeModLevel(1);
    else if (c == L'l')
      toggleModLogging();
    else if (c == L'0')
      resetModLevel();
    else if (c == L'k')
    {
      s.sidebarIndex = std::max(0, s.sidebarIndex - 1);
      moved();
    }
    else if (c == L'j')
    {
      s.sidebarIndex = std::min(count - 1, s.sidebarIndex + 1);
      moved();
    }
    else if (c == L'm')
    {
      s.sidebar = false;
      s.sidebarFocus = false;
    }
  }

  static void onKey(KEY_EVENT_RECORD const& key)
  {
    if (!key.bKeyDown)
      return;

    if (s.typing)
    {
      onTyping(key);
      return;
    }

    wchar_t c = key.uChar.UnicodeChar;
    bool ctrl = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);

    if (s.help)
    {
      s.help = false;
      return;
    }

    // Ctrl+C copies here, it doesn't close the game
    if (ctrl && key.wVirtualKeyCode == 'C')
    {
      copySelection();
      return;
    }

    if (s.picker)
    {
      onPickerKey(key, c);
      return;
    }

    if (s.sidebarFocus)
    {
      onSidebarKey(key, c);
      return;
    }

    // A text in the details panel (the crash log) scrolls with the arrows
    if (s.details && s.detailText)
    {
      int page = std::max(1, s_layout.height / 2 - 2);
      switch (key.wVirtualKeyCode)
      {
      case VK_UP:
        s.detailScroll = std::max(0, s.detailScroll - 1);
        return;
      case VK_DOWN:
        s.detailScroll++;
        return;
      case VK_PRIOR:
        s.detailScroll = std::max(0, s.detailScroll - page);
        return;
      case VK_NEXT:
        s.detailScroll += page;
        return;
      case VK_HOME:
        s.detailScroll = 0;
        return;
      default:
        break;
      }
    }

    for (WORD repeat = 0; repeat < std::max<WORD>(key.wRepeatCount, 1); repeat++)
    {
      switch (key.wVirtualKeyCode)
      {
      case VK_UP:
        move(-1);
        continue;
      case VK_DOWN:
        move(1);
        continue;
      case VK_PRIOR:
        move(-std::max(1, s_layout.listRows - 1));
        continue;
      case VK_NEXT:
        move(std::max(1, s_layout.listRows - 1));
        continue;
      default:
        break;
      }
      break;
    }

    switch (key.wVirtualKeyCode)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
      return;
    case VK_HOME:
      pause();
      select(0);
      return;
    case VK_END:
      follow();
      return;
    case VK_RETURN:
      if (s.selected && s.selected->row < 0)
        setCollapsed(*s.selected, std::nullopt);
      else if (s.details)
        closeDetails();
      else
        s.details = true;
      return;
    case VK_SPACE:
      if (s.selected)
        setCollapsed(*s.selected, std::nullopt);
      return;
    case VK_LEFT:
      if (s.selected)
      {
        if (s.selected->row >= 0)
          s.selected->row = -1;
        else
          setCollapsed(*s.selected, true);
      }
      return;
    case VK_RIGHT:
      if (s.selected)
        setCollapsed(*s.selected, false);
      return;
    case VK_TAB:
      if (!s.sidebar)
      {
        s.sidebar = true;
        saveState();
      }
      s.sidebarFocus = true;
      return;
    case VK_ESCAPE:
      if (s.details)
        closeDetails();
      else if (!s.search.empty())
        s.search.clear();
      else if (s.minLevel > Severity::Trace)
      {
        s.minLevel = Severity::Trace;
        saveState();
      }
      else if (s.historyName)
        closeHistory();
      else if (!s.follow)
        follow();
      return;
    default:
      break;
    }

    switch (c)
    {
    case L'k':
      move(-1);
      break;
    case L'j':
      move(1);
      break;
    case L'g':
      pause();
      select(0);
      break;
    case L'G':
      follow();
      break;
    case L'/':
      s.typing = true;
      break;
    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
      s.minLevel = Severity::Trace + (c - L'1');
      flash(fmt::format("Showing {} and above", levelName(s.minLevel)));
      saveState();
      break;
    case L'm':
      s.sidebar = !s.sidebar;
      s.sidebarFocus = false;
      saveState();
      break;
    case L'c':
      setAllCollapsed(true);
      break;
    case L'e':
      setAllCollapsed(false);
      break;
    case L'f':
    case L'p':
      if (s.follow)
        pause();
      else
        follow();
      break;
    case L'd':
      if (s.details)
        closeDetails();
      else
        s.details = true;
      break;
    case L'n':
      jump(1, false);
      break;
    case L'N':
      jump(-1, false);
      break;
    case L']':
      jump(1, true);
      break;
    case L'[':
      jump(-1, true);
      break;
    case L't':
      nextTimeMode();
      break;
    case L'b':
      devloop::addMarker();
      flash("Marker added");
      break;
    case L'R':
      flash("Restarting the game…");
      devloop::restart();
      break;
    case L'o':
      openCrash();
      break;
    case L'y':
      copySelection();
      break;
    case L'Y':
      copySelection(true);
      break;
    case L'E':
      copyProblems();
      break;
    case L'`':
      console::showGame();
      break;
    case L'w':
      exportView();
      break;
    case L'W':
    {
      std::error_code error;
      std::filesystem::create_directories(exportsDir(), error);
      file::openFolder(exportsDir());
      break;
    }
    case L'L':
      openPicker();
      break;
    case L'v':
      s.watchPanel = !s.watchPanel;
      flash(s.watchPanel ? "Watched values shown" : "Watched values hidden");
      saveState();
      break;
    case L'x':
      clearLogs();
      break;
    case L'?':
    case L'h':
      s.help = true;
      break;
    default:
      break;
    }
  }

  // ! --- Mouse --- !

  static void onClick(int x, int y, bool doubleClick)
  {
    if (s.help)
    {
      s.help = false;
      return;
    }

    // A banner does what its keys do
    if (y >= 1 && y < s_layout.listTop && y - 1 < static_cast<int>(s_layout.banners.size()))
    {
      if (s_layout.banners[y - 1] == BANNER_REBUILT)
      {
        flash("Restarting the game…");
        devloop::restart();
      }
      else
        openCrash();
      return;
    }

    int listRow = y - s_layout.listTop;
    if (listRow < 0 || listRow >= s_layout.listRows)
      return;

    if (x < s_layout.sidebarWidth)
    {
      // The first row of the sidebar is its title
      int index = s_layout.sidebarTop + listRow - 1;
      if (listRow == 0 || index >= static_cast<int>(s_sidebarMods.size()))
        return;
      s.sidebarFocus = true;
      s.sidebarIndex = index;
      toggleMod(index);
      return;
    }

    int index = s_layout.top + listRow;
    if (index >= static_cast<int>(s_view.size()))
      return;

    s.sidebarFocus = false;
    pause();
    select(index);
    if (s_view[index].row < 0)
      setCollapsed(s_view[index], std::nullopt);
    else if (doubleClick)
      s.details = !s.details;
  }

  static void onMouse(MOUSE_EVENT_RECORD const& mouse)
  {
    int x = mouse.dwMousePosition.X;
    int y = mouse.dwMousePosition.Y - console::windowTop();

    if (mouse.dwEventFlags & MOUSE_WHEELED)
    {
      auto delta = static_cast<short>(HIWORD(mouse.dwButtonState));
      if (x < s_layout.sidebarWidth)
      {
        int count = static_cast<int>(s_sidebarMods.size());
        s.sidebarIndex = std::clamp(s.sidebarIndex + (delta > 0 ? -3 : 3), 0, std::max(0, count - 1));
      }
      else
        scroll(delta > 0 ? -3 : 3);
      return;
    }

    bool pressed = mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED;
    bool wasPressed = s_buttons & FROM_LEFT_1ST_BUTTON_PRESSED;
    bool rightPressed = mouse.dwButtonState & RIGHTMOST_BUTTON_PRESSED;
    bool rightWasPressed = s_buttons & RIGHTMOST_BUTTON_PRESSED;
    s_buttons = mouse.dwButtonState;

    // A right click copies the line under it, like a quick y
    if (rightPressed && !rightWasPressed)
    {
      int index = s_layout.top + y - s_layout.listTop;
      if (x >= s_layout.sidebarWidth && y >= s_layout.listTop && y - s_layout.listTop < s_layout.listRows &&
          index < static_cast<int>(s_view.size()))
      {
        pause();
        select(index);
        copySelection();
      }
      return;
    }

    if (mouse.dwEventFlags & DOUBLE_CLICK)
      onClick(x, y, true);
    else if (pressed && !wasPressed)
      onClick(x, y, false);
  }

  // ! --- Drawing --- !

  static void drawTitle(Cells& row, model::Model const& model)
  {
    row.put(" ◆ Improved Logger ", {TITLE_FG, TITLE_BG, true});
    row.put("  ", {BAR_FG, BAR_BG});
    row.put(fmt::format("{} logs", compact(model.logs)), {BAR_FG, BAR_BG});

    auto warnings = model.severityCounts[Severity::Warning - Severity::Trace];
    auto errors = model.severityCounts[Severity::Error - Severity::Trace];
    if (warnings)
      row.put(fmt::format("  ⚠ {}", compact(warnings)), {WARNING_COLOR, BAR_BG});
    if (errors)
      row.put(fmt::format("  ✖ {}", compact(errors)), {ERROR_COLOR, BAR_BG, true});

    if (s.minLevel > Severity::Trace)
      row.put(fmt::format("   │  {}+", levelName(s.minLevel)), {KEY_FG, BAR_BG});
    if (!s.search.empty())
      row.put(fmt::format("   │  search \"{}\"", s.search), {KEY_FG, BAR_BG});

    int hidden = static_cast<int>(s.hiddenMods.size());
    if (query().badRegex)
      row.put("   │  bad regex", {ERROR_COLOR, BAR_BG, true});
    if (hidden)
      row.put(fmt::format("   │  {} hidden", hidden), {KEY_FG, BAR_BG});
    if (s.timeMode != TimeMode::Clock)
      row.put(fmt::format("   │  time {}", timeModeName(s.timeMode)), {KEY_FG, BAR_BG});

    if (s.historyName)
      row.right(fmt::format("◀ {} · Esc back to live ", *s.historyName), {PAUSED, BAR_BG, true});
    else if (s.follow)
      row.right("● LIVE ", {LIVE, BAR_BG, true});
    else
    {
      auto fresh = model.logs - std::min(model.logs, s.logsAtPause);
      // New errors while looking elsewhere are worth knowing about
      auto freshErrors = model.severityCounts[Severity::Error - Severity::Trace] - s.errorsAtPause;
      std::string text = fresh ? fmt::format("❚❚ PAUSED  +{} new  End ", compact(fresh)) : std::string("❚❚ PAUSED  End ");
      if (freshErrors > 0)
      {
        auto errors = fmt::format("✖ {} new  ", freshErrors);
        row.pad(row.used() + row.left() - text::width(text) - text::width(errors), {-1, BAR_BG});
        row.put(errors, {ERROR_COLOR, BAR_BG, true});
      }
      row.right(text, {PAUSED, BAR_BG, true});
    }
  }

  static void drawHeader(Cells& row, settings::Config const& config, model::Group const& group, bool selected)
  {
    int bg = selected ? SELECT_BG : -1;
    row.put(group.collapsed ? " ▸ " : " ▾ ", {group.color, bg, true});
    row.put(group.source.name, {group.color, bg, true});
    if (config.threadGroups && look::showsThread(group.source.thread))
      row.put(fmt::format(" · {}", group.source.thread), {DIM, bg});

    row.put(fmt::format("   {} {}", group.logs, group.logs == 1 ? "log" : "logs"), {DIM, bg});
    if (group.warnings)
      row.put(fmt::format("  ⚠ {}", group.warnings), {WARNING_COLOR, bg});
    if (group.errors)
      row.put(fmt::format("  ✖ {}", group.errors), {ERROR_COLOR, bg, true});

    auto time = group.firstTime == group.lastTime ? group.firstTime : fmt::format("{} – {}", group.firstTime, group.lastTime);
    if (row.left() > text::width(time) + 2)
      row.right(time + " ", {DIM, bg});
  }

  static void drawMarker(Cells& row, model::Group const& group, bool selected)
  {
    int bg = selected ? SELECT_BG : -1;
    auto const& marker = group.rows.front().record;
    row.put(" ━━ ", {group.color, bg, true});
    row.put(fmt::format("{} · {} ", marker.content, marker.time), {group.color, bg, true});
    std::string rule;
    for (int i = 0; i < row.left(); i++)
      rule += "━";
    row.put(rule, {group.color, bg});
  }

  // "+12ms", "+1.25s", "+2m05s"
  static std::string span(int64_t millis)
  {
    millis = std::max<int64_t>(millis, 0);
    if (millis < 1000)
      return fmt::format("+{}ms", millis);
    if (millis < 60000)
      return fmt::format("+{:.2f}s", millis / 1000.0);
    return fmt::format("+{}m{:02}s", millis / 60000, millis / 1000 % 60);
  }

  static int64_t millisOf(Line const& line)
  {
    if (line.row)
      return line.row->record.millis;
    if (line.group->marker)
      return line.group->rows.front().record.millis;
    return -1;
  }

  // The time column for the line at this index, as the time mode says
  static std::string timeFor(std::vector<Line> const& lines, int index)
  {
    auto const& record = lines[index].row->record;
    switch (s.timeMode)
    {
    case TimeMode::SincePrevious:
      for (int i = index - 1; i >= 0; i--)
      {
        auto millis = millisOf(lines[i]);
        if (millis >= 0)
          return span(record.millis - millis);
      }
      return "";
    case TimeMode::SinceGroup:
      return span(record.millis - lines[index].group->startMillis);
    default:
      return record.time;
    }
  }

  // The message with every match of the search marked
  static void putMarked(Cells& row, std::string_view message, Style style, int reserve)
  {
    auto const& marks = query().marks;
    if (marks.empty())
    {
      row.put(message, style, reserve);
      return;
    }

    size_t from = 0;
    while (from < message.size() && row.left() > reserve)
    {
      // The nearest of the searched words
      size_t found = std::string_view::npos;
      size_t length = 0;
      for (auto const& mark : marks)
      {
        auto at = text::findCaseless(message, mark, from);
        if (at < found && !mark.empty())
        {
          found = at;
          length = mark.size();
        }
      }
      if (found == std::string_view::npos)
      {
        row.put(message.substr(from), style, reserve);
        return;
      }
      row.put(message.substr(from, found - from), style, reserve);
      row.put(message.substr(found, length), {MATCH_FG, MATCH_BG, true}, reserve);
      from = found + length;
    }
  }

  static void drawRow(Cells& row, settings::Config const& config, model::Group const& group, model::Row const& logRow, std::string_view time, bool selected)
  {
    auto const& record = logRow.record;
    int bg = selected ? SELECT_BG : -1;
    auto [prefixColor, messageColor] = look::severityColors(record.severity);

    row.put("   ", {-1, bg});
    row.put("│ ", {group.color, bg});
    // The column keeps the clock's width in every time mode
    int timeWidth = static_cast<int>(record.time.size());
    row.put(fmt::format("{:>{}}", time, timeWidth), {s.timeMode == TimeMode::Clock ? DIM : KEY_FG, bg});
    row.put(" ", {-1, bg});
    row.put(look::severityName(record.severity), {prefixColor, bg, record.severity >= Severity::Warning});
    row.put(" ", {-1, bg});

    // Threads share the mod's group: the ones other than Main are named on their lines
    if (!config.threadGroups && look::showsThread(record.source.thread))
      row.put(fmt::format("[{}] ", record.source.thread), {look::GUIDE_COLOR, bg});

    if (record.nest > 0)
    {
      std::string guides;
      for (int i = 0; i < record.nest; i++)
        guides += config.nestGuides ? "│ " : "  ";
      row.put(guides, {look::GUIDE_COLOR, bg});
    }

    auto lines = text::splitLines(record.content);
    std::string more = lines.size() > 1 ? fmt::format("  ⏎ +{}", lines.size() - 1) : std::string();
    std::string badge = logRow.count > 1 ? fmt::format(" ×{} ", logRow.count) : std::string();
    int reserve = text::width(more) + (badge.empty() ? 0 : text::width(badge) + 2);

    putMarked(row, lines.empty() ? std::string_view() : lines[0], {messageColor, bg}, reserve);
    if (!more.empty())
      row.put(more, {DIM, bg});
    if (!badge.empty())
    {
      row.put("  ", {-1, bg});
      row.put(badge, {16, group.color, true});
    }
  }

  static void drawSidebarRow(Cells& row, model::Model& model, int listRow)
  {
    if (listRow == 0)
    {
      row.put(" MODS", {s.sidebarFocus ? KEY_FG : BAR_FG, -1, true});
      row.put(s.sidebarFocus ? "  Enter card · ? keys" : "  Tab", {DIM});
      return;
    }

    int index = s_layout.sidebarTop + listRow - 1;
    if (index >= static_cast<int>(s_sidebarMods.size()))
      return;

    auto stats = model.modStats(s_sidebarMods[index]);
    if (!stats)
      return;

    bool selected = s.sidebarFocus && index == s.sidebarIndex;
    int bg = selected ? SELECT_BG : -1;
    bool hidden = s.hiddenMods.contains(stats->name);
    row.put(hidden ? " ○ " : " ● ", {hidden ? DIM : stats->color, bg});

    // The mod's own log level, changed with + and -
    std::string level;
    int levelColor = DIM;
    if (stats->mod && !stats->mod->isLoggingEnabled())
    {
      level = "off ";
      levelColor = ERROR_COLOR;
    }
    else if (stats->mod)
      level = std::string(levelName(stats->mod->getLogLevel()).substr(0, 1)) + " ";

    // A mod writing many logs a second shows its rate instead of its count
    auto now = static_cast<int64_t>(asp::SystemTime::now().timeSinceEpoch().millis());
    auto rate = stats->rate(now);
    bool noisy = rate >= NOISY_RATE;
    auto count = noisy ? fmt::format("{:.0f}/s", rate) : compact(stats->logs);
    int nameColor = hidden ? DIM : stats->errors ? ERROR_COLOR : stats->warnings ? WARNING_COLOR : 252;
    int tail = text::width(count) + text::width(level) + 1;
    row.put(stats->name, {nameColor, bg}, tail + 1);
    row.pad(row.used() + row.left() - tail, {-1, bg});
    row.put(level, {levelColor, bg});
    row.right(count + " ", {noisy ? PAUSED : DIM, bg, noisy});
  }

  static std::vector<std::pair<std::string_view, std::string_view>> const& hints()
  {
    static std::vector<std::pair<std::string_view, std::string_view>> keys = {
        {"↑↓", "move"},
        {"Space", "fold"},
        {"Enter", "details"},
        {"/", "search"},
        {"1-5", "level"},
        {"Tab", "mods"},
        {"b", "mark"},
        {"n", "next issue"},
        {"t", "time"},
        {"f", "pause"},
        {"y", "copy"},
        {"?", "help"},
        {"L", "old logs"},
        {"w", "save"},
        {"c e", "fold all"},
        {"x", "clear"},
    };
    return keys;
  }

  static void drawBottom(Cells& row)
  {
    if (s.typing)
    {
      row.put(" / ", {KEY_FG, BAR_BG, true});
      row.put(s.search, {255, BAR_BG});
      row.put("█", {KEY_FG, BAR_BG});
      row.right(s.searches.empty() ? "Enter keep · Esc clear " : "↑↓ earlier · Enter keep · Esc clear ", {DIM, BAR_BG});
      return;
    }

    if (!s.flash.empty() && std::chrono::steady_clock::now() < s.flashUntil)
    {
      row.put(" ✓ " + s.flash, {LIVE, BAR_BG, true});
      return;
    }

    row.put(" ", {-1, BAR_BG});
    for (auto [key, label] : hints())
    {
      if (row.left() < text::width(key) + text::width(label) + 3)
        break;
      row.put(key, {KEY_FG, BAR_BG, true});
      row.put(fmt::format(" {}   ", label), {BAR_FG, BAR_BG});
    }
  }

  // "now", "3s ago", "2m ago"
  static std::string ago(int64_t millis)
  {
    if (millis < 1000)
      return "now";
    if (millis < 60000)
      return fmt::format("{}s ago", millis / 1000);
    return fmt::format("{}m ago", millis / 60000);
  }

  static void drawWatch(Cells& row, model::Watch const& watch, int64_t now)
  {
    auto age = fmt::format("×{} · {} ", watch.updates, ago(now - watch.millis));
    row.put("  ● ", {watch.color});
    row.put(watch.modName + "  ", {watch.color, -1, true});
    row.put(watch.name, {252, -1, true});
    row.put(" = ", {DIM});
    row.put(watch.value, {255}, text::width(age) + 2);
    row.right(age, {DIM});
  }

  static void drawPickerRow(Cells& row, int listRow)
  {
    if (listRow == 0)
    {
      row.put("  OLDER LOGS", {KEY_FG, -1, true});
      row.put("  ↑↓ choose · Enter opens · Esc back", {DIM});
      return;
    }

    // Keeps the chosen file on the screen
    int rows = std::max(1, s_layout.listRows - 1);
    int top = std::max(0, s.pickerIndex - rows + 1);
    int index = top + listRow - 1;
    if (index >= static_cast<int>(s.picker->size()))
      return;

    auto const& path = (*s.picker)[index];
    bool selected = index == s.pickerIndex;
    int bg = selected ? SELECT_BG : -1;
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    row.put(selected ? "  ▸ " : "    ", {KEY_FG, bg, true});
    row.put(path.filename().string(), {selected ? 255 : 252, bg, selected});
    row.right(fmt::format("{} KB ", (size + 1023) / 1024), {DIM, bg});
  }

  static std::vector<std::string> helpLines()
  {
    return {
        "",
        "  KEYS",
        "",
        "  ↑ ↓  j k         move, scrolling up pauses the live view",
        "  PgUp PgDn        a page up or down",
        "  Home g / End G   the first line / back to live",
        "  Space  ← →       fold or unfold a group",
        "  Enter            fold a group, or open a log in full",
        "  c / e            fold / unfold every group",
        "  /                search as you type, Esc clears it. Words must all match,",
        "                   -word must not, \"a phrase\", mod:name thread:name level:warn re:regex",
        "  1 2 3 4 5        show from TRACE, DEBUG, INFO, WARN or ERROR",
        "  Tab  m           the mods list: Space hides a mod, s shows only it, a all",
        "  y  Ctrl+C        copy the log, or the whole group on its header",
        "  f                pause or resume the live view",
        "  d                details panel",
        "  x                clear the screen (the log file keeps everything)",
        "  Esc              close the panel, clear the search, the level, back to live",
        "",
        "  DEVELOPING",
        "",
        "  b                a marker line, also from the game with the keybind in the settings",
        "  n / N            the next / previous warning, error or marker",
        "  ] / [            the next / previous group",
        "  t                time: clock, since the log before, since the group's start",
        "  Shift+R          restart the game, the banner says when a .geode was rebuilt",
        "  o                open the crash log of the last session",
        "  L                open an older log file of Geode, Esc goes back to live",
        "  v                show or hide the values mods watch through the API",
        "  w / W            save what the filters show to a file / open the folder",
        "  Y                copy as Markdown, for a bug report",
        "  E                copy every warning and error, by mod, as Markdown",
        "  `                back to the game, Ctrl+Shift+L in the game comes back here",
        "",
        "  MODS LIST",
        "",
        "  Space            hide or show the mod's logs here",
        "  s / a            only this mod / every mod",
        "  Enter            the mod's card: version, hooks, logs per second",
        "  + / -  l  0      more or fewer of its logs, logging off (log file too), back to all",
        "                   these stay for the next launches",
        "",
        "  MOUSE",
        "",
        "  wheel            scroll",
        "  click            select, a header folds its group",
        "  double click     open a log in full",
        "  right click      copy the line",
        "  click a mod      hide or show it",
        "",
        "  Mode, folding, colors, pauses and filters are in the mod's settings in Geode.",
        "  Any key closes this.",
    };
  }

  static std::vector<std::string> detailLines(model::Model& model, std::optional<Line> const& line, int width, std::string& title)
  {
    if (s.detailText)
    {
      title = s.detailText->first;
      std::vector<std::string> lines;
      for (auto& part : text::wrap(text::clean(s.detailText->second), width - 4))
        lines.push_back("  " + part);
      return lines;
    }

    if (s.detailMod)
    {
      title = fmt::format(" {} ", *s.detailMod);
      std::vector<std::string> lines;
      if (auto stats = model.modStats(*s.detailMod))
      {
        auto now = static_cast<int64_t>(asp::SystemTime::now().timeSinceEpoch().millis());
        lines.push_back(fmt::format("  {} logs · {} warnings · {} errors · {:.1f} logs/s over the last {} s", stats->logs,
                                    stats->warnings, stats->errors, stats->rate(now), model::RATE_SECONDS));
      }
      std::lock_guard lock(s_cardMutex);
      if (s_card && s_card->name == *s.detailMod)
        lines.insert(lines.end(), s_card->lines.begin(), s_card->lines.end());
      return lines;
    }

    if (!line)
    {
      title = " Details ";
      return {"  Nothing selected"};
    }

    auto const& group = *line->group;
    auto modID = group.source.mod ? std::string(group.source.mod->getID()) : std::string("—");

    if (!line->row)
    {
      title = fmt::format(" {} · {} ", group.source.name, modID);
      return {
          fmt::format("  {} logs in {} rows, {} warnings, {} errors", group.logs, group.rows.size(), group.warnings, group.errors),
          fmt::format("  from {} to {} on the {} thread", group.firstTime, group.lastTime, group.source.thread.empty() ? "unnamed" : group.source.thread),
      };
    }

    auto const& record = line->row->record;
    title = fmt::format(" {} · {} · {} · {} · {}{} ", group.source.name, modID, record.source.thread.empty() ? "—" : record.source.thread,
                        record.time, levelName(record.severity), line->row->count > 1 ? fmt::format(" · ×{}", line->row->count) : "");

    std::vector<std::string> lines;
    for (auto& part : text::wrap(record.content, width - 4))
      lines.push_back("  " + part);
    return lines;
  }

  static void render()
  {
    auto config = settings::get();
    auto& model = viewModel();

    int height = std::max(console::windowRows(), 4);
    int width = std::max(console::windowColumns(), 20);
    std::vector<std::string> screen;
    screen.reserve(height);

    std::lock_guard lock(model.mutex);
    auto lines = flatten(model);
    int count = static_cast<int>(lines.size());

    s_view.clear();
    for (auto const& line : lines)
      s_view.push_back(line.ref);

    // Banners under the title: rebuilt mods, the last crash
    std::vector<std::pair<std::string, int>> banners;
    std::vector<int> bannerKinds;
    auto updated = devloop::updatedMods();
    if (!updated.empty())
    {
      std::string ids;
      for (auto const& id : updated)
        ids += (ids.empty() ? "" : ", ") + id;
      banners.emplace_back(fmt::format(" ⟳ {} rebuilt · Shift+R or a click restarts the game to load it", ids), 221);
      bannerKinds.push_back(BANNER_REBUILT);
    }
    if (auto crash = devloop::lastCrash())
    {
      banners.emplace_back(fmt::format(" ✖ The last session crashed at {} · o or a click opens the crash log", crash->time), ERROR_COLOR);
      bannerKinds.push_back(BANNER_CRASH);
    }
    int bannerRows = std::min(static_cast<int>(banners.size()), std::max(0, height - 4));

    // The details panel takes the lower part of the screen
    int body = height - 2 - bannerRows;
    std::string detailTitle;
    std::vector<std::string> details;
    int detailRows = 0;
    if (s.details && !s.help)
    {
      details = detailLines(model, selectedLine(lines), width, detailTitle);
      // Long texts get more room and scroll with the arrows
      int room = s.detailText || s.detailMod ? body * 2 / 3 : body / 2;
      detailRows = std::clamp(static_cast<int>(details.size()) + 1, 3, std::max(3, room));
      int shown = detailRows - 1;
      s.detailScroll = std::clamp(s.detailScroll, 0, std::max(0, static_cast<int>(details.size()) - shown));
      details.erase(details.begin(), details.begin() + s.detailScroll);
    }
    // Watched values sit between the list and the details
    std::vector<model::Watch> watches;
    if (s.watchPanel && !s.help && !s.historyName)
      watches = model::get().watches;
    int watchRows = watches.empty() ? 0 : std::min(static_cast<int>(watches.size()), 8) + 1;
    watchRows = std::min(watchRows, std::max(0, body - detailRows - 3));

    int listRows = std::max(1, body - detailRows - watchRows);

    // Where the list starts: the end while live, around the selection otherwise
    int top = 0;
    int selectedIndex = -1;
    int lastTop = std::max(0, count - listRows);
    if (s.follow)
      top = lastTop;
    else
    {
      selectedIndex = indexOf(lines, s.selected);
      if (selectedIndex < 0 && count > 0)
        selectedIndex = std::clamp(s.selectedIndex, 0, count - 1);
      top = indexOf(lines, s.top);
      if (top < 0)
        top = s_layout.top;
      top = std::clamp(top, 0, lastTop);
      if (selectedIndex >= 0)
      {
        if (selectedIndex < top)
          top = selectedIndex;
        if (selectedIndex >= top + listRows)
          top = selectedIndex - listRows + 1;
        s.selected = lines[selectedIndex].ref;
      }
      s.selectedIndex = selectedIndex;
      if (count > 0)
        s.top = lines[top].ref;
    }

    // Mods with the most logs first
    s_sidebarMods.clear();
    if (s.sidebar)
    {
      std::vector<model::ModStats const*> mods;
      for (auto const& mod : model.mods)
        mods.push_back(&mod);
      std::ranges::stable_sort(mods, [](auto a, auto b)
                               { return a->logs > b->logs; });
      for (auto mod : mods)
        s_sidebarMods.push_back(mod->name);
      s.sidebarIndex = std::clamp(s.sidebarIndex, 0, std::max(0, static_cast<int>(mods.size()) - 1));
    }
    int sidebarWidth = s.sidebar ? std::min(SIDEBAR_WIDTH, width / 3) : 0;
    int sidebarTop = s_layout.sidebarTop;
    int sidebarRows = listRows - 1;
    if (s.sidebarIndex < sidebarTop)
      sidebarTop = s.sidebarIndex;
    if (s.sidebarIndex >= sidebarTop + sidebarRows)
      sidebarTop = s.sidebarIndex - sidebarRows + 1;
    sidebarTop = std::max(0, sidebarTop);

    s_layout = Layout{
        .height = height,
        .width = width,
        .listRows = listRows,
        .sidebarWidth = sidebarWidth,
        .top = top,
        .sidebarTop = sidebarTop,
        .listTop = 1 + bannerRows,
        .banners = std::move(bannerKinds),
    };

    Cells title(width, BAR_BG);
    drawTitle(title, model);
    screen.push_back(title.finish());

    for (int i = 0; i < bannerRows; i++)
    {
      Cells banner(width, 236);
      banner.put(banners[i].first, {banners[i].second, 236, true});
      screen.push_back(banner.finish());
    }

    auto help = s.help ? helpLines() : std::vector<std::string>();
    for (int i = 0; i < listRows; i++)
    {
      std::string row;
      if (sidebarWidth)
      {
        Cells side(sidebarWidth - 1);
        drawSidebarRow(side, model, i);
        row += side.finish();
        Cells border(1);
        border.put("│", {BORDER});
        row += border.finish();
      }

      Cells main(width - sidebarWidth);
      if (s.picker && !s.help)
        drawPickerRow(main, i);
      else if (s.help)
      {
        if (i < static_cast<int>(help.size()))
        {
          // Titles are the lines in capitals
          bool heading = help[i].size() > 2 && help[i].find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
          main.put(help[i], {heading ? KEY_FG : 252, -1, heading});
        }
      }
      else if (top + i < count)
      {
        auto const& line = lines[top + i];
        bool selected = top + i == selectedIndex;
        if (line.group->marker)
          drawMarker(main, *line.group, selected);
        else if (line.row)
          drawRow(main, *config, *line.group, *line.row, timeFor(lines, top + i), selected);
        else
          drawHeader(main, *config, *line.group, selected);
      }
      else if (count == 0 && i == 0)
        main.put(filtering() ? "  Nothing matches. Esc clears the search and the level" : "  No logs yet", {DIM});
      row += main.finish();
      screen.push_back(std::move(row));
    }

    if (watchRows)
    {
      Cells head(width);
      head.put("── WATCHING ", {KEY_FG, -1, true});
      head.put(fmt::format("{} values · v hides ", watches.size()), {DIM});
      std::string rule;
      for (int i = 0; i < head.left(); i++)
        rule += "─";
      head.put(rule, {BORDER});
      screen.push_back(head.finish());

      auto now = static_cast<int64_t>(asp::SystemTime::now().timeSinceEpoch().millis());
      for (int i = 0; i < watchRows - 1; i++)
      {
        Cells row(width);
        drawWatch(row, watches[i], now);
        screen.push_back(row.finish());
      }
    }

    if (detailRows)
    {
      Cells head(width);
      head.put("──", {BORDER});
      head.put(detailTitle, {KEY_FG, -1, true});
      std::string rule;
      for (int i = 0; i < head.left(); i++)
        rule += "─";
      head.put(rule, {BORDER});
      screen.push_back(head.finish());

      for (int i = 0; i < detailRows - 1; i++)
      {
        Cells row(width);
        bool last = i == detailRows - 2 && static_cast<int>(details.size()) > detailRows - 1;
        if (last)
        {
          // The mod card scrolls nowhere, its keys move through the mods list
          auto hint = s.detailText  ? "↑↓ scroll · y copies it all"
                      : s.detailMod ? "Esc closes"
                                    : "y copies it all";
          row.put(fmt::format("  … {} more lines · {}", details.size() - i, hint), {DIM});
        }
        else if (i < static_cast<int>(details.size()))
          row.put(details[i], {252});
        screen.push_back(row.finish());
      }
    }

    // The last column of the last row stays empty, writing there scrolls some consoles
    Cells bottom(width - 1, BAR_BG);
    drawBottom(bottom);
    screen.push_back(bottom.finish());

    std::string frame = "\x1b[?25l";
    for (size_t i = 0; i < screen.size(); i++)
      frame += fmt::format("\x1b[{};1H{}", i + 1, screen[i]);
    console::write(frame);
  }

  // ! --- Screen thread --- !

  static void enter()
  {
    if (s_input == INVALID_HANDLE_VALUE)
      s_input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (s_input != INVALID_HANDLE_VALUE)
    {
      GetConsoleMode(s_input, &s_inputMode);
      // No quick edit, so clicks reach the screen, and no Ctrl+C closing the game
      SetConsoleMode(s_input, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
      FlushConsoleInputBuffer(s_input);
    }

    console::write("\x1b[?1049h\x1b[?25l\x1b[2J");
    s_active = true;
    s_dirty = true;
  }

  static void leave()
  {
    console::write("\x1b[0m\x1b[2J\x1b[?25h\x1b[?1049l");
    if (s_input != INVALID_HANDLE_VALUE)
      SetConsoleMode(s_input, s_inputMode);
    s_active = false;
  }

  static void readInput()
  {
    DWORD pending = 0;
    if (!GetNumberOfConsoleInputEvents(s_input, &pending) || pending == 0)
      return;

    INPUT_RECORD records[64];
    DWORD read = 0;
    if (!ReadConsoleInputW(s_input, records, 64, &read))
      return;

    for (DWORD i = 0; i < read; i++)
    {
      switch (records[i].EventType)
      {
      case KEY_EVENT:
        onKey(records[i].Event.KeyEvent);
        break;
      case MOUSE_EVENT:
        onMouse(records[i].Event.MouseEvent);
        break;
      case WINDOW_BUFFER_SIZE_EVENT:
        break;
      default:
        continue;
      }
      s_dirty = true;
    }
  }

  static void run()
  {
    uint64_t drawnRevision = 0;
    auto lastFrame = std::chrono::steady_clock::now();
    auto lastPoll = lastFrame;
    std::chrono::steady_clock::duration frameGap = FRAME_TIME;
    size_t updatedCount = 0;
    bool flashing = false;

    while (true)
    {
      bool wanted = s_wanted.load(std::memory_order::relaxed);
      if (wanted != s_active)
        wanted ? enter() : leave();

      // Rebuilt mods are looked for in both modes, the stream prints a line for them
      if (std::chrono::steady_clock::now() - lastPoll >= std::chrono::seconds(1))
      {
        devloop::poll();
        lastPoll = std::chrono::steady_clock::now();
        auto updated = devloop::updatedMods().size();
        if (updated != updatedCount)
          s_dirty = true;
        updatedCount = updated;
      }
      if (s_wake.exchange(false))
        s_dirty = true;

      if (!s_active)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      if (s_input != INVALID_HANDLE_VALUE)
      {
        if (WaitForSingleObject(s_input, 16) == WAIT_OBJECT_0)
          readInput();
      }
      else
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

      // The "copied" note goes away by itself
      bool flashNow = !s.flash.empty() && std::chrono::steady_clock::now() < s.flashUntil;
      if (flashing != flashNow)
        s_dirty = true;
      flashing = flashNow;

      auto revision = model::get().revision.load(std::memory_order::relaxed);
      auto now = std::chrono::steady_clock::now();
      // "3s ago" and logs per second move without new logs
      if (now - lastFrame >= std::chrono::seconds(1))
        s_dirty = true;
      if ((s_dirty || revision != drawnRevision) && now - lastFrame >= frameGap)
      {
        render();
        s_dirty = false;
        drawnRevision = revision;
        lastFrame = now;

        // A slow frame (a regex over a big history) waits longer before the next one,
        // the log thread needs the model's lock too
        auto took = std::chrono::steady_clock::now() - now;
        frameGap = std::max<std::chrono::steady_clock::duration>(FRAME_TIME, took * 3);
      }
    }
  }

  void start()
  {
    if (s_started.exchange(true))
      return;
    loadState();
    applyModLevels();
    // Some mods set their own level while they load: once they all did, the saved ones win again
    GameEvent(GameEventType::Loaded).listen([]
                                            { applyModLevels(); })
        .leak();
    std::thread(run).detach();
  }

  void setActive(bool active)
  {
    s_wanted.store(active && console::isOpen(), std::memory_order::relaxed);
  }

  bool isActive()
  {
    return s_wanted.load(std::memory_order::relaxed);
  }
}
