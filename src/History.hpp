#pragma once

#include "Model.hpp"
#include "Settings.hpp"

#include <filesystem>
#include <memory>
#include <vector>

// Geode's log files of earlier sessions, read back into the interactive screen
namespace history
{
  // Older log files, newest first, without the current session's
  std::vector<std::filesystem::path> files();

  // The file's lines as logs, grouped and folded like live ones
  std::unique_ptr<model::Model> load(std::filesystem::path const& path, settings::Config const& config);
  // The same, added to a model that already has logs. Takes the model's lock
  void read(model::Model& model, std::filesystem::path const& path, settings::Config const& config);
}
