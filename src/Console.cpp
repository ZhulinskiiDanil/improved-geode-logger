#include "Console.hpp"

#include <Geode/Geode.hpp>
#include <atomic>

#include <Windows.h>

using namespace geode::prelude;

namespace console
{
  static HANDLE s_handle = INVALID_HANDLE_VALUE;
  static bool s_escapeCodes = false;
  static std::atomic<bool> s_grouping = false;

  // ! --- Geode's writes --- !

  // Geode prints to the console with WriteFile, the grouped lines go out with WriteConsoleW,
  // so every WriteFile that lands on a console is one of Geode's lines
  static bool isConsole(HANDLE file)
  {
    DWORD mode;
    return GetFileType(file) == FILE_TYPE_CHAR && GetConsoleMode(file, &mode);
  }

  static BOOL WINAPI WriteFileDetour(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped)
  {
    if (s_grouping.load(std::memory_order::relaxed) && isConsole(file))
    {
      if (written)
        *written = size;
      return TRUE;
    }
    return WriteFile(file, buffer, size, written, overlapped);
  }

  // ! --- Console --- !

  bool open()
  {
    // GetConsoleWindow says nothing in Windows Terminal, opening the console is the real check
    s_handle = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (s_handle == INVALID_HANDLE_VALUE)
    {
      log::info("No platform console, grouping is off");
      return false;
    }

    DWORD mode = 0;
    s_escapeCodes = GetConsoleMode(s_handle, &mode) &&
                    SetConsoleMode(s_handle, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // KernelBase holds the real WriteFile, the one in kernel32 only jumps there
    auto kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto address = kernelBase ? GetProcAddress(kernelBase, "WriteFile") : nullptr;
    if (!address)
      address = reinterpret_cast<FARPROC>(&WriteFile);

    auto hook = Mod::get()->hook(reinterpret_cast<void*>(address), &WriteFileDetour, "WriteFile");
    if (!hook)
    {
      log::error("Can't take over the console, Geode keeps printing it: {}", hook.unwrapErr());
      CloseHandle(s_handle);
      s_handle = INVALID_HANDLE_VALUE;
      return false;
    }

    return true;
  }

  bool isOpen()
  {
    return s_handle != INVALID_HANDLE_VALUE;
  }

  bool hasEscapeCodes()
  {
    return s_escapeCodes;
  }

  int columns()
  {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!isOpen() || !GetConsoleScreenBufferInfo(s_handle, &info) || info.dwSize.X <= 0)
      return 120;
    return info.dwSize.X;
  }

  int windowRows()
  {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!isOpen() || !GetConsoleScreenBufferInfo(s_handle, &info))
      return 0;
    return info.srWindow.Bottom - info.srWindow.Top + 1;
  }

  void write(std::string_view utf8)
  {
    if (!isOpen() || utf8.empty())
      return;

    auto wide = string::utf8ToWide(utf8);
    DWORD written;
    WriteConsoleW(s_handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
  }

  void setGrouping(bool enabled)
  {
    s_grouping.store(enabled && isOpen(), std::memory_order::relaxed);
  }
}
