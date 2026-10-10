#pragma once

#include <optional>
#include <string>
#include <vector>

// Things for the loop of building a mod and trying it in the game
namespace devloop
{
  // Remembers the builds in the mods folder and looks for the last crash. On the main thread, at load
  void start();

  // A marker line in the logs, from the console, the game's keybind or another mod, from any thread
  void addMarker(std::string const& label = "", std::string const& from = "");

  // Looks for builds that changed since the game started, about once a second
  void poll();

  // IDs of the mods whose .geode changed, the game needs a restart to load them
  std::vector<std::string> updatedMods();

  void restart();

  // A notification in the game for an error, so it isn't missed behind the game window.
  // At most one every few seconds, the ones in between are counted into the next
  void toast(std::string const& mod, int severity, std::string_view message);

  struct Crash
  {
    std::string path;
    // When it happened, in local time
    std::string time;
  };

  // The crash of the last session, if it ended with one and nobody looked at it yet
  std::optional<Crash> lastCrash();
  void dismissCrash();
  std::string readCrash();
}
