#include "Console.hpp"
#include "Grouper.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// ! --- Start --- !

$on_mod(Loaded)
{
  // No console window, nothing to group: Geode's log files are left alone either way
  if (!console::open())
    return;

  settings::load();

  log::LogEvent().listen([](log::BorrowedLog const& log)
                         {
                           grouper::handle(log);
                           return false; })
      .leak();
}
