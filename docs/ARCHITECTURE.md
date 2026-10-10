# How Improved Logger works

A map of the code and the reasons behind the parts that aren't obvious.

## The path of a log

1. A mod calls `log::info(...)`. Geode formats it and queues it for its log thread.
2. Geode's log thread writes it to the log file and to the console, then sends `LogEvent`.
3. **Console.cpp** drops Geode's own write to the console (see below), so only this mod draws there.
4. **Grouper.cpp** `grouper::handle` gets the `LogEvent`: filters it like Geode does (console level, the mod's level, focus and mute), makes a `Record`, pops errors up in the game, and adds it to the **Model**.
5. In the Stream mode it also prints the grouped line right away. In the Interactive mode **Tui.cpp** draws the model about 30 times a second.

## Files

| File | What it does |
| --- | --- |
| `main.cpp` | Start: opens the console, reads the earlier logs from the log file, listens to logs, the keybinds, the first-launch popup |
| `Console.cpp` | The console window: writing, its size, swapping Geode's `WriteFile`, the close button, bringing the console or the game forward |
| `Grouper.cpp` | Every log comes here first. The Stream mode: headers, folding by rewriting the lines above the cursor |
| `Model.cpp` | The history the screen shows: groups, folded repeats and repeating blocks, counts, watched values, at most 50 000 rows |
| `Tui.cpp` | The interactive screen: drawing, keys, mouse, panels, search, exports |
| `Search.cpp` | The search words, fields and regexes |
| `LogLine.cpp`, `History.cpp` | Reading Geode's log files back: the current one at start, older ones with `L` |
| `DevLoop.cpp` | Markers, rebuilt `.geode` files, restarting, the last crash, load problems, pop-ups |
| `Html.cpp` | The page `H` saves |
| `Api.cpp`, `include/ImprovedLogger.hpp` | The API for other mods |
| `Settings.cpp` | The mod's settings and Geode's console settings, as one snapshot any thread can read |
| `Text.cpp`, `Look.hpp`, `Levels.hpp`, `Record.hpp` | Small shared pieces |

`Text`, `Search`, `LogLine`, `Model` and `Html` don't include Geode, so `tests/` builds them alone (CI runs them with GCC too). They use `Levels.hpp` instead of `geode::Severity`. `Grouper.cpp` checks with a `static_assert` that the two still match.

## Decisions worth knowing

- **Geode's console writes are dropped by swapping `WriteFile` in `Geode.dll`'s import table, not with a hook.** The log thread calls `WriteFile` all the time. When any mod adds a hook, the page with the hook handlers is briefly not executable, and a thread running a handler at that moment crashes (`DEP violation`). That's what happened with a tulip hook and early load. The import table swap is plain code of this mod.
- **The swap remembers which handle is the console.** Asking `GetFileType` and `GetConsoleMode` on every write went through the console driver and was the slowest part of a log: a burst of 200 000 logs lost half of them in Geode's queue. With the answer cached, none are lost.
- **The screen lets go of the model before the console takes a frame.** The log thread needs the model's lock for every log, and writing a frame to the console takes a while.
- **The earlier logs are read with `std::ifstream`, not `file::readString`.** Geode's reader doesn't share the file with a writer, and Geode is still writing the current log.
- **The last crash is found from file times.** Geode deletes its "last crashed" flag before mods load. A crash log written while the previous session's log was open means that session crashed.
- **Restart brings the game forward first.** A minimized game stops its loop, and the restart waits for the main thread.
- **Saved mod levels are applied twice**: before the other mods load, and once the game loaded, since some mods set their own level while loading.
- **Errors from Geode itself don't pop up while the game loads.** They're the ones Geode's problems list shows, like "setting not found in savedata" after a mod update added a setting.
- **A click on a mod in the list only picks it.** Hiding takes a double click or Space: a stray click hid mods, and hidden mods stay hidden next time.

## Building and trying

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build          # installs into the game, the console shows "rebuilt"
cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
```

Press `Shift+R` in the console to restart into the new build.
