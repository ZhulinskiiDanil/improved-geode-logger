#pragma once

// Geode's log levels as plain numbers, for the parts that don't include Geode (and their tests).
// Grouper.cpp checks they still match geode::Severity
namespace level
{
  inline constexpr int Trace = -1;
  inline constexpr int Debug = 0;
  inline constexpr int Info = 1;
  inline constexpr int Warning = 2;
  inline constexpr int Error = 3;
}
