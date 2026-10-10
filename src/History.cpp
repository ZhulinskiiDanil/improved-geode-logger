#include "History.hpp"
#include "LogLine.hpp"
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

    std::optional<logline::Line> pending;
    auto flush = [&]
    {
      if (!pending)
        return;
      auto it = mods.find(pending->source);
      Mod* mod = it == mods.end() ? nullptr : it->second;

      Record record{
          .source = {mod, pending->source, pending->thread, mod ? std::string(mod->getID()) : std::string()},
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
      if (auto parsed = logline::parse(line))
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
