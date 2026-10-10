#pragma once

#include <Geode/loader/Types.hpp>
#include <string_view>
#include <utility>

// Colors and names shared by the console stream and the interactive screen
namespace look
{
  inline constexpr int GUIDE_COLOR = 240;
  inline constexpr int PLAIN_MOD_COLOR = 245;

  // Geode's console colors: one for the time and level, one for the message
  inline std::pair<int, int> severityColors(int severity)
  {
    switch (severity)
    {
    case geode::Severity::Trace:
      return {54, 254};
    case geode::Severity::Debug:
      return {243, 250};
    case geode::Severity::Info:
      return {45, 254};
    case geode::Severity::Warning:
      return {229, 230};
    case geode::Severity::Error:
      return {9, 224};
    default:
      return {7, 7};
    }
  }

  inline std::string_view severityName(int severity)
  {
    switch (severity)
    {
    case geode::Severity::Trace:
      return "TRACE";
    case geode::Severity::Debug:
      return "DEBUG";
    case geode::Severity::Info:
      return "INFO ";
    case geode::Severity::Warning:
      return "WARN ";
    case geode::Severity::Error:
      return "ERROR";
    default:
      return "?????";
    }
  }

  inline bool showsThread(std::string_view thread)
  {
    return !thread.empty() && thread != "Main";
  }
}
