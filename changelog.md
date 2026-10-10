# v1.5.0

- On the first launch without Geode's console, a popup offers to turn it on and restart
- The log levels set in the mods list stay for the next launches, 0 sets a mod back to everything
- Up and Down go through earlier searches while typing one, kept between launches
- `M` shows only what came after the last marker, and `t` has a time since the marker above
- The console window's close button is off (a setting): closing it closed the game too, without saving
- A banner when Geode couldn't load some mods, `P` (or a click) shows why
- Floods of logs no longer lose lines: 200 000 logs in a burst all get through (89 000 before). The console check of every Geode write is remembered, the screen lets go of the logs while the console takes a frame, and the clock is formatted once a second
- `Shift+R` restarts a minimized game too: it stopped its loop and never got to the restart
- Keys like `R`, `b` and `?` work in the mods list too
- The console window's title says Improved Logger instead of the game's path
- The help scrolls (Up, Down, the wheel, Page Down): it was cut off in smaller windows
- A click on a mod in the list picks it, a double click hides it (a stray click hid mods)
- What a `re:` search finds is marked in the lines too
- The history stays at 50 000 rows when one mod writes without a pause too: one long group kept growing
- `H` saves the logs as a page to open in a browser or send: groups fold, levels in color, a filter box
- Tests for the folding of repeats, the search and the reading of log files, run in CI

# v1.4.0

- The interactive console starts with what Geode logged before the mod loaded, read from the log file
- Errors pop up in the game too, so they aren't missed behind the game window (a setting: off, errors, or warnings too)
- `Ctrl+Shift+L` in the game brings the console forward, `` ` `` in the console brings the game back
- One group per mod across threads: lines from other threads show the thread's name (Separate threads is a setting)
- `E` copies every warning and error of the session as Markdown, grouped by mod
- A right click copies the line under it
- The mods list shows the logs per second of a mod writing 5 or more a second
- A paused view counts the new errors in the top bar
- A click on a banner does what its key does (restart, open the crash log)
- Older log files get their real dates, for the time between logs
- A slow frame (a regex over a big history) waits longer before the next one, so the log thread isn't held up
- A logo

# v1.3.0

- An API for other mods (`include/ImprovedLogger.hpp`, no hard dependency): `watch` pins values in a panel of the console that updates in place, `Timer` logs how long code took, `mark` puts a marker from code
- Smarter search: every word must match, `-word` must not, `"a phrase"`, `mod:`, `thread:`, `level:` and `re:` for a regex, every searched word is marked
- `L` opens an older log file of Geode in the console, with the same groups, search and filters
- `w` saves what the filters show to a file, `W` opens the folder, `Y` copies as Markdown
- `v` shows or hides the watched values
- Hidden mods, the level, the time format and the panels are kept between launches

# v1.2.0

- Markers: `b` in the console or a keybind in the game (Ctrl+Shift+M) puts a marker line into the logs, to see what got logged after you did something
- A banner when a mod's `.geode` is rebuilt, `Shift+R` restarts the game to load it
- A banner when the last session crashed, `o` opens its crash log in the console
- `n` / `N` jump to the next or previous warning, error or marker, `]` / `[` to the next or previous group
- `t` shows the time as the clock, since the log before, or since the group's start
- Mods list: the level of each mod's logs, `+` / `-` change it and `l` turns its logging off, right from the console
- Mods list: `Enter` opens a mod's card with its version, developers, hooks, patches and logs per second

# v1.1.0

- Interactive console: browse the logs with the keyboard and mouse
  - Fold and unfold groups, one by one or all at once
  - Search as you type, matches are marked
  - Show from a level up (keys 1 to 5)
  - Mods list: hide a mod or show only it, with the count of its logs
  - Details panel with the whole log, copy a log or a group to the clipboard
  - Pause the live view, the top bar counts the new logs meanwhile
  - Ctrl+C in the console copies instead of closing the game
- Stream mode keeps the old printed lines, in the mod's settings
- Loads early, so every other mod's logs are grouped from the start
- Fixed a crash of the log thread when a mod adds hooks while logs are printed

# v1.0.0

- Logs grouped by mod and thread under colored headers
- Repeated lines and repeating blocks fold into one with a counter
- Nesting guides for `log::NestScope`
- Empty line between bursts of logs
- Focus on mods and mute mods
