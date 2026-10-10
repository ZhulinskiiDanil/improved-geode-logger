#pragma once

// Improved Logger's API for other mods: values pinned in the console, timers and markers.
//
// No hard dependency: add "zhulis.improved-logger" to your mod.json's dependencies with
// "required": false (or copy this header). When Improved Logger isn't installed, watch and
// mark do nothing, timers still write their log.
//
//   improved_logger::watch("player x", player->getPositionX());  // pinned, updated in place
//   improved_logger::unwatch("player x");
//   improved_logger::mark("level started");                      // a marker line in the logs
//
//   {
//     improved_logger::Timer timer("load level");                // logs "⏱ load level: 12.40 ms"
//     loadLevel();
//   }

#include <Geode/loader/Dispatch.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace improved_logger
{
  namespace detail
  {
    // The calls reach Improved Logger by these IDs, they never change
    inline void watchText(std::string_view modID, std::string_view name, std::string_view value)
        GEODE_EVENT_EXPORT_ID_NORES(&watchText, (modID, name, value), "zhulis.improved-logger/watch");

    inline void unwatchText(std::string_view modID, std::string_view name)
        GEODE_EVENT_EXPORT_ID_NORES(&unwatchText, (modID, name), "zhulis.improved-logger/unwatch");

    inline void markText(std::string_view modID, std::string_view label)
        GEODE_EVENT_EXPORT_ID_NORES(&markText, (modID, label), "zhulis.improved-logger/mark");
  }

  // Pins a value in the console under your mod's name. Calling it again with the same name
  // updates it in place, so a value that changes every frame doesn't flood the logs
  template <class T>
  inline void watch(std::string_view name, T const& value)
  {
    detail::watchText(geode::getMod()->getID(), name, fmt::format("{}", value));
  }

  // Takes a pinned value away, or every value of your mod with no name
  inline void unwatch(std::string_view name = "")
  {
    detail::unwatchText(geode::getMod()->getID(), name);
  }

  // A marker line in the logs with your mod's name and this label
  inline void mark(std::string_view label = "")
  {
    detail::markText(geode::getMod()->getID(), label);
  }

  // Logs how long it lived, or how long until stop(), as a debug log of your mod
  class Timer
  {
  public:
    explicit Timer(std::string label)
        : m_label(std::move(label)), m_start(std::chrono::steady_clock::now()) {}

    Timer(Timer const&) = delete;
    Timer& operator=(Timer const&) = delete;

    ~Timer()
    {
      if (m_running)
        stop();
    }

    // Milliseconds since the start, without stopping
    double elapsed() const
    {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    }

    double stop()
    {
      m_running = false;
      auto millis = elapsed();
      geode::log::debug("⏱ {}: {:.2f} ms", m_label, millis);
      return millis;
    }

  private:
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
    bool m_running = true;
  };
}
