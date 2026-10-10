#include "Settings.hpp"
#include "Console.hpp"
#include "Tui.hpp"

#include <Geode/Geode.hpp>
#include <mutex>

using namespace geode::prelude;

namespace settings
{
  static std::mutex s_mutex;
  static std::shared_ptr<Config const> s_config = std::make_shared<Config>();

  static Mod* geodeMod()
  {
    // While early mods load, Geode itself is not marked loaded yet
    return Loader::get()->getInstalledMod("geode.loader");
  }

  static std::vector<std::string> parseModList(std::string_view text)
  {
    std::vector<std::string> mods;
    for (auto part : string::split(text, ","))
    {
      auto mod = string::toLower(string::trim(std::string(part)));
      if (!mod.empty())
        mods.push_back(std::move(mod));
    }
    return mods;
  }

  static int parseLevel(std::string_view level)
  {
    if (level == "trace")
      return Severity::Trace;
    if (level == "debug")
      return Severity::Debug;
    if (level == "warn")
      return Severity::Warning;
    if (level == "error")
      return Severity::Error;
    return Severity::Info;
  }

  static Collapse parseCollapse(std::string_view mode)
  {
    if (mode == "Off")
      return Collapse::Off;
    if (mode == "Identical")
      return Collapse::Identical;
    return Collapse::Similar;
  }

  std::shared_ptr<Config const> read()
  {
    auto mod = Mod::get();
    auto config = std::make_shared<Config>();

    config->enabled = mod->getSettingValue<bool>("enabled");
    config->interactive = mod->getSettingValue<std::string>("mode") == "Interactive";
    config->groupByMod = mod->getSettingValue<bool>("group-by-mod");
    config->modColors = mod->getSettingValue<bool>("mod-colors");
    config->nestGuides = mod->getSettingValue<bool>("nest-guides");
    config->gapSeconds = mod->getSettingValue<double>("gap");
    config->collapse = parseCollapse(mod->getSettingValue<std::string>("collapse"));
    config->focusMods = parseModList(mod->getSettingValue<std::string>("focus-mods"));
    config->mutedMods = parseModList(mod->getSettingValue<std::string>("muted-mods"));

    if (auto geode = geodeMod())
    {
      config->consoleLevel = parseLevel(geode->getSettingValue<std::string>("console-log-level"));
      config->millis = geode->getSettingValue<bool>("log-milliseconds");
    }
    return config;
  }

  static void reload()
  {
    auto config = read();
    console::setGrouping(config->enabled);
    bool interactive = config->enabled && config->interactive;

    {
      std::lock_guard lock(s_mutex);
      s_config = std::move(config);
    }

    tui::setActive(interactive);
  }

  void load()
  {
    reload();

    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>)
                               { reload(); });

    if (auto geode = geodeMod())
    {
      listenForSettingChanges<std::string>("console-log-level", [](std::string)
                                           { reload(); }, geode);
      listenForSettingChanges<bool>("log-milliseconds", [](bool)
                                    { reload(); }, geode);
    }
  }

  std::shared_ptr<Config const> get()
  {
    std::lock_guard lock(s_mutex);
    return s_config;
  }
}
