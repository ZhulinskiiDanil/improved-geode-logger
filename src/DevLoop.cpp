#include "DevLoop.hpp"
#include "Console.hpp"
#include "Grouper.hpp"
#include "Model.hpp"
#include "Settings.hpp"
#include "Text.hpp"
#include "Tui.hpp"

#include <Geode/Geode.hpp>
#include <asp/time/SystemTime.hpp>
#include <fmt/chrono.h>
#include <atomic>
#include <map>
#include <mutex>
#include <regex>

#include <Windows.h>

using namespace geode::prelude;

namespace devloop
{
  static constexpr int MARKER_COLOR = 213;

  static std::mutex s_mutex;
  static std::map<std::string, std::filesystem::file_time_type> s_builds;
  static std::vector<std::string> s_updated;
  static std::optional<Crash> s_crash;
  // Until the builds are remembered, every one of them would look new
  static std::atomic<bool> s_started = false;

  // ! --- Markers --- !

  void addMarker(std::string const& label, std::string const& from)
  {
    auto config = settings::get();
    if (!console::isGrouping())
      return;

    auto now = asp::SystemTime::now();
    auto millis = static_cast<int64_t>(now.timeSinceEpoch().millis());
    auto local = asp::localtime(now.to_time_t());
    auto time = config->millis ? fmt::format("{:%H:%M:%S}.{:03}", local, millis % 1000) : fmt::format("{:%H:%M:%S}", local);

    Record marker{
        .kind = RecordKind::Marker,
        .source = {nullptr, "Marker", ""},
        .color = MARKER_COLOR,
        .time = time,
        .millis = millis,
    };

    {
      auto& model = model::get();
      std::lock_guard lock(model.mutex);
      // "Mark 3", or "Mark 3 · Icon Mayhem: level started" from a mod
      marker.content = fmt::format("Mark {}", model.markers + 1);
      if (!label.empty() || !from.empty())
        marker.content += fmt::format(" · {}{}{}", from, !from.empty() && !label.empty() ? ": " : "", label);
      model.addMarker(marker);
    }

    if (!tui::isActive())
      grouper::printRule(fmt::format("{} · {}", marker.content, marker.time), MARKER_COLOR);
  }

  // ! --- Builds --- !

  static std::map<std::string, std::filesystem::file_time_type> scanBuilds()
  {
    std::map<std::string, std::filesystem::file_time_type> builds;
    std::error_code error;
    for (auto const& entry : std::filesystem::directory_iterator(dirs::getModsDir(), error))
    {
      if (entry.path().extension() != ".geode")
        continue;
      auto time = entry.last_write_time(error);
      if (!error)
        builds[entry.path().stem().string()] = time;
    }
    return builds;
  }

  void poll()
  {
    if (!s_started)
      return;

    auto builds = scanBuilds();
    std::vector<std::string> fresh;
    {
      std::lock_guard lock(s_mutex);
      for (auto const& [id, time] : builds)
      {
        auto it = s_builds.find(id);
        if (it != s_builds.end() && it->second == time)
          continue;
        s_builds[id] = time;
        if (std::ranges::find(s_updated, id) == s_updated.end())
        {
          s_updated.push_back(id);
          fresh.push_back(id);
        }
      }
    }

    // The interactive screen shows a banner, the stream gets a line
    if (!tui::isActive() && console::isGrouping())
    {
      for (auto const& id : fresh)
        grouper::printRule(fmt::format("{} was rebuilt · restart the game to load it", id), 221);
    }
  }

  std::vector<std::string> updatedMods()
  {
    std::lock_guard lock(s_mutex);
    return s_updated;
  }

  // ! --- Toasts --- !

  static constexpr auto TOAST_GAP = std::chrono::seconds(3);
  static std::optional<std::chrono::steady_clock::time_point> s_lastToast;
  static int s_skippedToasts = 0;
  // While the game loads, Geode's own errors are the ones its problems list already shows,
  // like a setting a mod update added ("not found in savedata")
  static std::atomic<bool> s_gameLoaded = false;

  // ! --- Load problems --- !

  static std::vector<std::string> s_problems;

  static std::string causeName(LoadProblem const& problem)
  {
    if (auto mod = std::get_if<Mod*>(&problem.cause); mod && *mod)
      return std::string((*mod)->getName());
    if (auto metadata = std::get_if<ModMetadata>(&problem.cause))
      return std::string(metadata->getName());
    if (auto path = std::get_if<std::filesystem::path>(&problem.cause))
      return path->filename().string();
    return "A mod";
  }

  // Geode's messages are made for its popups: "<cy>install</c>", "<mod:geode.node-ids>"
  static std::string withoutTags(std::string const& message)
  {
    static std::regex const colors("</?c[a-z]?>");
    static std::regex const mods("<mod:([^>]+)>");
    return std::regex_replace(std::regex_replace(message, colors, ""), mods, "$1");
  }

  $on_game(Loaded)
  {
    s_gameLoaded = true;

    std::vector<std::string> problems;
    for (auto const& problem : Loader::get()->getLoadProblems())
    {
      auto line = fmt::format("{}: {}", causeName(problem), withoutTags(problem.message));
      // Geode keeps outdated mods out of its own menu, they are here for completeness
      if (!problem.isProblemTheUserShouldCareAbout())
        line += " (outdated)";
      problems.push_back(std::move(line));
    }

    {
      std::lock_guard lock(s_mutex);
      s_problems = problems;
    }

    if (!problems.empty() && !tui::isActive() && console::isGrouping())
      grouper::printRule(fmt::format("{} {} Geode couldn't load fully · {}", problems.size(), problems.size() == 1 ? "mod" : "mods", problems.front()), 221);
  }

  std::vector<std::string> loadProblems()
  {
    std::lock_guard lock(s_mutex);
    return s_problems;
  }

  void dismissLoadProblems()
  {
    std::lock_guard lock(s_mutex);
    s_problems.clear();
  }

  void toast(std::string const& mod, int severity, std::string_view message)
  {
    if (!s_gameLoaded && mod == "Geode")
      return;

    std::string text;
    {
      std::lock_guard lock(s_mutex);
      auto now = std::chrono::steady_clock::now();
      if (s_lastToast && now - *s_lastToast < TOAST_GAP)
      {
        s_skippedToasts++;
        return;
      }
      s_lastToast = now;

      // The first line, short enough for the notification
      auto line = text::splitLines(message).front();
      auto shown = text::fit(line, 70);
      text = fmt::format("{}: {}{}", mod, shown, shown.size() < line.size() ? "..." : "");
      if (s_skippedToasts)
        text += fmt::format(" (+{} more)", s_skippedToasts);
      s_skippedToasts = 0;
    }

    Loader::get()->queueInMainThread([text = std::move(text), severity]
                                     { Notification::create(text, severity >= Severity::Error ? NotificationIcon::Error : NotificationIcon::Warning, NOTIFICATION_LONG_TIME)->show(); });
  }

  void restart()
  {
    Loader::get()->queueInMainThread([]
                                     { game::restart(true); });
  }

  // ! --- Last crash --- !

  struct FileTimes
  {
    ULONGLONG created = 0;
    ULONGLONG written = 0;
  };

  static ULONGLONG ticks(FILETIME time)
  {
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  }

  static std::optional<FileTimes> timesOf(std::filesystem::path const& path)
  {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
      return std::nullopt;
    return FileTimes{ticks(data.ftCreationTime), ticks(data.ftLastWriteTime)};
  }

  static std::optional<std::filesystem::path> newestLog(std::filesystem::path const& folder, std::filesystem::path const& skip = {})
  {
    std::optional<std::filesystem::path> newest;
    ULONGLONG newestTime = 0;
    std::error_code error;
    for (auto const& entry : std::filesystem::directory_iterator(folder, error))
    {
      if (entry.path().extension() != ".log" || (!skip.empty() && std::filesystem::equivalent(entry.path(), skip, error)))
        continue;
      auto times = timesOf(entry.path());
      if (times && times->created > newestTime)
      {
        newestTime = times->created;
        newest = entry.path();
      }
    }
    return newest;
  }

  static std::string localTime(ULONGLONG time)
  {
    FILETIME file{static_cast<DWORD>(time), static_cast<DWORD>(time >> 32)};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
      return "?";
    return fmt::format("{:02}.{:02} {:02}:{:02}:{:02}", local.wDay, local.wMonth, local.wHour, local.wMinute, local.wSecond);
  }

  // Geode deletes its "last crashed" file before mods load, so the crash is found from the files:
  // a crash log written while the previous session's log file was open
  static void findLastCrash()
  {
    auto current = log::getCurrentLogPath();
    auto previous = newestLog(dirs::getGeodeLogDir(), current);
    auto crash = newestLog(dirs::getCrashlogsDir());
    if (!previous || !crash)
      return;

    auto previousTimes = timesOf(*previous);
    auto currentTimes = timesOf(current);
    auto crashTimes = timesOf(*crash);
    if (!previousTimes || !currentTimes || !crashTimes)
      return;

    if (crashTimes->written >= previousTimes->created && crashTimes->written <= currentTimes->created)
      s_crash = Crash{crash->string(), localTime(crashTimes->written)};
  }

  std::optional<Crash> lastCrash()
  {
    std::lock_guard lock(s_mutex);
    return s_crash;
  }

  void dismissCrash()
  {
    std::lock_guard lock(s_mutex);
    s_crash.reset();
  }

  std::string readCrash()
  {
    auto crash = lastCrash();
    if (!crash)
      return "";
    return file::readString(std::filesystem::path(crash->path)).unwrapOr("The crash log can't be read");
  }

  // ! --- Start --- !

  void start()
  {
    {
      std::lock_guard lock(s_mutex);
      s_builds = scanBuilds();
    }
    s_started = true;
    findLastCrash();

    if (auto crash = lastCrash(); crash && !tui::isActive() && console::isGrouping())
      grouper::printRule(fmt::format("The last session crashed at {} · {}", crash->time, crash->path), 203);
  }
}
