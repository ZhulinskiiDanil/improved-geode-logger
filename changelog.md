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
