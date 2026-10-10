#include "History.hpp"
#include "Look.hpp"
#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <charconv>
#include <ctime>
#include <fstream>
#include <unordered_map>

using namespace geode::prelude;

namespace history
{
  std::vector<std::filesystem::path> files()
  {
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    std::error_code error;
    auto current = log::getCurrentLogPath();
    for (auto const& entry : std::filesystem::directory_iterator(dirs::getGeodeLogDir(), error))
    {
      if (entry.path().extension() != ".log" || std::filesystem::equivalent(entry.path(), current, error))
        continue;
      found.emplace_back(entry.last_write_time(error), entry.path());
    }

    std::ranges::sort(found, std::greater{});
    std::vector<std::filesystem::path> paths;
    for (auto& [time, path] : found)
      paths.push_back(std::move(path));
    return paths;
  }

  static bool isDigit(char c)
  {
    return c >= '0' && c <= '9';
  }

  static int severityOf(std::string_view level)
  {
    if (level == "TRACE")
      return Severity::Trace;
    if (level == "DEBUG")
      return Severity::Debug;
    if (level == "WARN ")
      return Severity::Warning;
    if (level == "ERROR")
      return Severity::Error;
    return Severity::Info;
  }

  struct Parsed
  {
    std::string time;
    int64_t millis = 0;
    int severity = 0;
    std::string thread;
    std::string source;
    int nest = 0;
    std::string content;
  };

  // "13:38:54.123 DEBUG [Main] [Geode]:   message", the thread and the milliseconds may be missing
  static std::optional<Parsed> parseLine(std::string_view line)
  {
    if (line.size() < 15 || !isDigit(line[0]) || !isDigit(line[1]) || line[2] != ':' || line[5] != ':')
      return std::nullopt;

    Parsed parsed;
    auto space = line.find(' ');
    if (space == std::string_view::npos)
      return std::nullopt;
    parsed.time = std::string(line.substr(0, space));

    int hours = (line[0] - '0') * 10 + (line[1] - '0');
    int minutes = (line[3] - '0') * 10 + (line[4] - '0');
    int seconds = (line[6] - '0') * 10 + (line[7] - '0');
    int millis = space > 9 ? std::atoi(std::string(line.substr(9, space - 9)).c_str()) : 0;
    parsed.millis = ((hours * 60 + minutes) * 60 + seconds) * 1000LL + millis;

    auto rest = line.substr(space + 1);
    if (rest.size() < 7)
      return std::nullopt;
    parsed.severity = severityOf(rest.substr(0, 5));
    rest.remove_prefix(6);

    // One bracket is the mod, two are the thread and the mod
    auto bracket = [&](std::string& out) -> bool
    {
      if (rest.empty() || rest[0] != '[')
        return false;
      auto close = rest.find(']');
      if (close == std::string_view::npos)
        return false;
      out = std::string(rest.substr(1, close - 1));
      rest.remove_prefix(close + 1);
      return true;
    };

    std::string first;
    if (!bracket(first))
      return std::nullopt;
    if (rest.starts_with(" ["))
    {
      rest.remove_prefix(1);
      parsed.thread = std::move(first);
      if (!bracket(parsed.source))
        return std::nullopt;
    }
    else
      parsed.source = std::move(first);

    if (!rest.starts_with(": "))
      return std::nullopt;
    rest.remove_prefix(2);

    size_t spaces = 0;
    while (spaces < rest.size() && rest[spaces] == ' ')
      spaces++;
    parsed.nest = static_cast<int>(spaces + 1) / 2;
    parsed.content = std::string(rest.substr(spaces));
    return parsed;
  }

  // Local midnight of the day in "Geode 2026-10-10 15.23.03.log", in milliseconds since the epoch
  static int64_t dayStart(std::filesystem::path const& path)
  {
    auto name = path.filename().string();
    std::tm day{};
    auto number = [&](size_t from, size_t length, int& out)
    {
      auto begin = name.data() + from;
      return std::from_chars(begin, begin + length, out).ec == std::errc();
    };
    if (name.starts_with("Geode ") && name.size() >= 16 && name[10] == '-' && name[13] == '-' &&
        number(6, 4, day.tm_year) && number(11, 2, day.tm_mon) && number(14, 2, day.tm_mday))
    {
      day.tm_year -= 1900;
      day.tm_mon -= 1;
      day.tm_isdst = -1;
      auto time = std::mktime(&day);
      if (time != -1)
        return static_cast<int64_t>(time) * 1000;
    }
    return 0;
  }

  // Geode's readString doesn't share the file with a writer, and Geode is still writing the current log
  static std::string readShared(std::filesystem::path const& path)
  {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  }

  void read(model::Model& model, std::filesystem::path const& path, settings::Config const& config)
  {
    auto text = readShared(path);
    std::lock_guard lock(model.mutex);
    // The lines only have the time of day, the file's name has the day
    auto start = dayStart(path);
    int64_t lastTime = -1;
    int64_t days = 0;

    // Names in the file back to mods, for their colors and cards
    std::unordered_map<std::string, Mod*> mods;
    for (auto mod : Loader::get()->getAllMods())
      mods.emplace(std::string(mod->getName()), mod);

    std::optional<Parsed> pending;
    auto flush = [&]
    {
      if (!pending)
        return;
      auto it = mods.find(pending->source);
      Mod* mod = it == mods.end() ? nullptr : it->second;

      Record record{
          .source = {mod, pending->source, pending->thread},
          .severity = pending->severity,
          .nest = pending->nest,
          .color = config.modColors && mod ? text::colorFor(mod->getID()) : look::PLAIN_MOD_COLOR,
          .time = pending->time,
          .millis = start + days * 86400000 + pending->millis,
          .content = text::clean(pending->content),
      };
      if (config.collapse == settings::Collapse::Similar)
        record.pattern = text::withoutNumbers(record.content);
      model.add(std::move(record), config);
      pending.reset();
    };

    for (auto line : text::splitLines(text))
    {
      if (auto parsed = parseLine(line))
      {
        flush();
        // A session that went past midnight
        if (parsed->millis < lastTime)
          days++;
        lastTime = parsed->millis;
        pending = std::move(parsed);
      }
      // Lines of a log written over several lines
      else if (pending)
        pending->content += "\n" + std::string(line);
    }
    flush();
  }

  std::unique_ptr<model::Model> load(std::filesystem::path const& path, settings::Config const& config)
  {
    auto model = std::make_unique<model::Model>();
    read(*model, path, config);
    return model;
  }
}
