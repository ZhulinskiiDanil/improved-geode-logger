#pragma once

#include <Geode/loader/Log.hpp>

namespace grouper
{
  // Called for every log Geode prints, from whatever thread prints it
  void handle(geode::log::BorrowedLog const& log);
}
