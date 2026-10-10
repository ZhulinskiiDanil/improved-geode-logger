#pragma once

// The interactive console: a full screen over the console window to browse the logs
// with the keyboard and mouse. It draws from its own thread, the logs come from the model
namespace tui
{
  void start();

  // Turns the screen on or off, the console stream takes over while it's off
  void setActive(bool active);
  bool isActive();
}
