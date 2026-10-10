#pragma once

#include <string_view>

// The game's console window. Geode keeps writing its lines to it: those get dropped
// while grouping is on, and the grouped lines are written here instead
namespace console
{
  // Finds the console Geode opened and starts catching Geode's writes to it.
  // False when there is no console (Show Platform Console is off), then the mod stays out of the way
  bool open();
  bool isOpen();

  // Colors and rewriting the last line. Off on consoles without escape codes (Wine, old Windows)
  bool hasEscapeCodes();

  // Width of the console buffer, where long lines wrap
  int columns();

  // Rows of the visible window, the cursor can't go above it
  int windowRows();
  int windowColumns();
  // Top row of the window in the buffer, mouse positions count from the buffer's top
  int windowTop();

  void write(std::string_view utf8);

  // Brings the console's window in front of the game, or the game in front of the console
  void showConsole();
  void showGame();

  // Closing the console window closes the game too, without saving: its close button can be turned off
  void setClosable(bool closable);

  // While off, Geode writes to the console as usual
  void setGrouping(bool enabled);
  bool isGrouping();
}
