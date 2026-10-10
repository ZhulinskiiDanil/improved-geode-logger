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

  using WriteFileType = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
  static std::atomic<WriteFileType> s_writeFile = &WriteFile;

  // Geode prints to the console with WriteFile, the grouped lines go out with WriteConsoleW,
  // so every WriteFile from Geode that lands on a console is one of Geode's lines
  static bool isConsole(HANDLE file)
  {
    // Geode writes to the same two handles all session long, its console and its log file:
    // the last answer for each saves two system calls on every log
    static std::atomic<HANDLE> knownConsole = nullptr;
    static std::atomic<HANDLE> knownOther = nullptr;
    if (file == knownConsole.load(std::memory_order::relaxed))
      return true;
    if (file == knownOther.load(std::memory_order::relaxed))
      return false;

    DWORD mode;
    bool console = GetFileType(file) == FILE_TYPE_CHAR && GetConsoleMode(file, &mode);
    (console ? knownConsole : knownOther).store(file, std::memory_order::relaxed);
    return console;
  }

  static BOOL WINAPI geodeWriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped)
  {
    if (s_grouping.load(std::memory_order::relaxed) && isConsole(file))
    {
      if (written)
        *written = size;
      return TRUE;
    }
    return s_writeFile.load(std::memory_order::relaxed)(file, buffer, size, written, overlapped);
  }

  // Swaps WriteFile in Geode.dll's import table, so only Geode's own calls come here.
  // Not a hook: the log thread calls WriteFile all the time, and a hook's code becomes
  // briefly non-executable whenever another mod adds hooks, which crashes that thread
  static bool takeGeodeWrites()
  {
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(L"Geode.dll"));
    if (!base)
      return false;

    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto const& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress)
      return false;

    bool taken = false;
    for (auto library = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); library->Name; library++)
    {
      if (!library->OriginalFirstThunk)
        continue;

      auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + library->OriginalFirstThunk);
      auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + library->FirstThunk);
      for (; names->u1.AddressOfData; names++, slots++)
      {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
          continue;
        auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
        if (std::string_view(name) != "WriteFile")
          continue;

        auto slot = reinterpret_cast<void**>(&slots->u1.Function);
        DWORD protection;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
          continue;
        s_writeFile.store(reinterpret_cast<WriteFileType>(*slot));
        InterlockedExchangePointer(slot, reinterpret_cast<void*>(&geodeWriteFile));
        VirtualProtect(slot, sizeof(void*), protection, &protection);
        taken = true;
      }
    }
    return taken;
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

    if (!takeGeodeWrites())
    {
      log::error("Can't take over the console, Geode keeps printing it");
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

  int windowColumns()
  {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!isOpen() || !GetConsoleScreenBufferInfo(s_handle, &info))
      return 120;
    return info.srWindow.Right - info.srWindow.Left + 1;
  }

  int windowTop()
  {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!isOpen() || !GetConsoleScreenBufferInfo(s_handle, &info))
      return 0;
    return info.srWindow.Top;
  }

  void write(std::string_view utf8)
  {
    if (!isOpen() || utf8.empty())
      return;

    auto wide = string::utf8ToWide(utf8);
    DWORD written;
    WriteConsoleW(s_handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
  }

  // ! --- Windows --- !

  // Windows lets only the foreground's own thread hand the focus over, so it borrows that thread's input
  static void bringForward(HWND window)
  {
    if (!window)
      return;
    if (IsIconic(window))
      ShowWindow(window, SW_RESTORE);

    auto foreground = GetForegroundWindow();
    auto foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    auto thread = GetCurrentThreadId();
    bool attached = foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
    SetForegroundWindow(window);
    BringWindowToTop(window);
    if (attached)
      AttachThreadInput(thread, foregroundThread, FALSE);
  }

  void showConsole()
  {
    // In Windows Terminal the console's own window is hidden, the terminal owns it
    auto window = GetConsoleWindow();
    auto owner = window ? GetAncestor(window, GA_ROOTOWNER) : nullptr;
    bringForward(owner ? owner : window);
  }

  static BOOL CALLBACK findGameWindow(HWND window, LPARAM found)
  {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || window == GetConsoleWindow())
      return TRUE;
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
  }

  void showGame()
  {
    HWND game = nullptr;
    EnumWindows(findGameWindow, reinterpret_cast<LPARAM>(&game));
    bringForward(game);
  }

  void setClosable(bool closable)
  {
    // Windows Terminal owns its tabs, only the classic console window has this button
    auto window = GetConsoleWindow();
    auto menu = window ? GetSystemMenu(window, FALSE) : nullptr;
    if (menu)
      EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | (closable ? MF_ENABLED : MF_GRAYED));
  }

  void setGrouping(bool enabled)
  {
    s_grouping.store(enabled && isOpen(), std::memory_order::relaxed);
  }

  bool isGrouping()
  {
    return s_grouping.load(std::memory_order::relaxed);
  }
}
