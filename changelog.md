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
