#include "Console.hpp"
#include "Grouper.hpp"
#include "Settings.hpp"
#include "Tui.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

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

  tui::start();
  settings::load();
}
