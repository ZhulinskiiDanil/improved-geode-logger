// The other side of include/ImprovedLogger.hpp: other mods' calls land here
#define GEODE_DEFINE_EVENT_EXPORTS
#include "../include/ImprovedLogger.hpp"

#include "DevLoop.hpp"
#include "Model.hpp"
#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <asp/time/SystemTime.hpp>

using namespace geode::prelude;

static std::string modName(std::string_view modID)
{
  auto mod = Loader::get()->getInstalledMod(modID);
  return mod ? std::string(mod->getName()) : std::string(modID);
}

void improved_logger::detail::watchText(std::string_view modID, std::string_view name, std::string_view value)
{
  auto& model = model::get();
  std::lock_guard lock(model.mutex);
  model.setWatch(model::Watch{
      .modID = std::string(modID),
      .modName = modName(modID),
      .color = text::colorFor(modID),
      .name = std::string(name),
      .value = text::clean(value),
      .millis = static_cast<int64_t>(asp::SystemTime::now().timeSinceEpoch().millis()),
  });
}

void improved_logger::detail::unwatchText(std::string_view modID, std::string_view name)
{
  auto& model = model::get();
  std::lock_guard lock(model.mutex);
  model.removeWatch(std::string(modID), std::string(name));
}

void improved_logger::detail::markText(std::string_view modID, std::string_view label)
{
  devloop::addMarker(std::string(label), modName(modID));
}
