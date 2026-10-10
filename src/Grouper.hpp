#pragma once

#include <Geode/loader/Log.hpp>

namespace grouper
{
  // Called for every log Geode prints, from whatever thread prints it
  void handle(geode::log::BorrowedLog const& log);

  // A rule across the stream with this text: markers, rebuilt mods, the last crash
  void printRule(std::string_view text, int color);
}
