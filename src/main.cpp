#include "Console.hpp"
#include "DevLoop.hpp"
#include "Grouper.hpp"
#include "History.hpp"
#include "Model.hpp"
#include "Settings.hpp"
#include "Tui.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// ! --- First launch --- !

// Without Geode's console there is nothing to show: once, offer to turn it on
$on_game(Loaded)
{
  if (console::isOpen() || Mod::get()->getSavedValue<bool>("asked-for-console", false))
    return;
  Mod::get()->setSavedValue("asked-for-console", true);

  createQuickPopup(
      "Improved Logger",
      "The logs show up in Geode's console window, which is off. Turn on <cy>Show Platform Console</c> and restart the game?",
      "Not now", "Turn on",
      [](auto, bool turnOn)
      {
        auto geode = Loader::get()->getInstalledMod("geode.loader");
        if (!turnOn || !geode)
          return;
        geode->setSettingValue<bool>("show-platform-console", true);
        game::restart(true);
      });
}

// ! --- Start --- !

$on_mod(Loaded)
{
  // No console window, nothing to group: Geode's log files are left alone either way
  if (!console::open())
    return;

  // Listening first: once the settings turn grouping on, no log falls in between
  log::LogEvent().listen([](log::BorrowedLog const& log)
                         {
                           grouper::handle(log);
                           return false; })
      .leak();

  // What Geode logged before this mod loaded is in its log file: the interactive screen starts with it.
  // Grouping is still off, so nothing logged meanwhile goes missing from the console
  log::flush();
  auto config = settings::read();
  history::read(model::get(), log::getCurrentLogPath(), *config);
  {
    auto& model = model::get();
    std::lock_guard lock(model.mutex);
    if (model.logs > 0)
      model.addMarker(Record{
          .kind = RecordKind::Marker,
          .source = {nullptr, "Marker", ""},
          .color = 245,
          .time = model.groups.back().lastTime,
          .millis = model.groups.back().rows.back().record.millis,
          .content = "Improved Logger loaded · the logs above come from the log file",
      });
    // The developer's own markers still start at 1
    model.markers = 0;
  }

  tui::start();
  settings::load();
  devloop::start();

  // The console in front of the game, without alt-tabbing to it
  listenForKeybindSettingPresses("console-key", [](Keybind const&, bool down, bool repeat, double)
                                 {
                                   if (down && !repeat)
                                     console::showConsole(); });

  // A marker straight from the game, without going to the console
  listenForKeybindSettingPresses("marker-key", [](Keybind const&, bool down, bool repeat, double)
                                 {
                                   if (down && !repeat)
                                     devloop::addMarker(); });
}
