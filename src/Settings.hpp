#pragma once

#include <memory>
#include <string>
#include <vector>

namespace settings
{
  enum class Collapse
  {
    Off,
    Identical,
    Similar,
  };

  struct Config
  {
    bool enabled = true;
    // A full screen to browse the logs, or lines printed as they come
    bool interactive = true;
    bool groupByMod = true;
    bool modColors = true;
    bool nestGuides = true;
    double gapSeconds = 1.5;
    Collapse collapse = Collapse::Similar;

    // Lowercased mod IDs and names
    std::vector<std::string> focusMods;
    std::vector<std::string> mutedMods;

    // Geode's own console settings, so the console shows what it showed before
    int consoleLevel = 1;
    bool millis = false;
  };

  void load();

  // Safe from any thread, the logs come from Geode's log worker
  std::shared_ptr<Config const> get();
}
