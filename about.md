# Improved Logger

Makes the platform console readable when many mods log at once, or one mod logs a lot.

## Interactive console

The console becomes a screen to browse the logs:

- **Groups**: logs that come one after another from the same mod sit in one group with its name, count, warnings and errors. Fold and unfold them with <cy>Space</c>, all at once with <cy>c</c> and <cy>e</c>. Lines from threads other than Main show the thread's name.
- **From the start**: the screen begins with what Geode logged before the mod loaded, read from the log file.
- **Search**: <cy>/</c> filters as you type and marks the matches, <cy>Up</c> and <cy>Down</c> bring back earlier searches. All words must match, `-word` must not, `"a phrase"` keeps its spaces, and `mod:`, `thread:`, `level:warn` and `re:` (a regex) look at one thing.
- **Levels**: <cy>1</c> to <cy>5</c> show from TRACE, DEBUG, INFO, WARN or ERROR up.
- **Mods**: <cy>Tab</c> opens the list of mods with their counts. Hide a mod, or show only it. Hidden mods, the level, the time format and the panels stay as you left them next time.
- **Details**: <cy>Enter</c> opens a log in full, <cy>y</c>, <cy>Ctrl+C</c> or a right click copies it, <cy>Y</c> copies it as Markdown for a bug report, <cy>E</c> copies every warning and error by mod.
- **Older logs**: <cy>L</c> opens one of Geode's earlier log files right in the console, with the same groups, search and filters.
- **Save**: <cy>w</c> writes what the filters show to a file, <cy>W</c> opens the folder.
- **Live or paused**: scrolling up pauses the view, the top bar counts the new logs and errors, <cy>End</c> goes back to them.
- **Mouse**: the wheel scrolls, a click selects or folds, a double click opens a log.

Press <cy>?</c> in the console for every key.

## For making mods

- **Errors in the game**: an error pops up as a notification in the game, so it isn't missed while the console is behind it. One every few seconds at most, only from the focused mods if there are some.
- **Game and console**: <cy>Ctrl+Shift+L</c> in the game brings the console forward, <cy>`</c> in the console brings the game back.
- **Markers**: <cy>b</c> in the console, or <cy>Ctrl+Shift+M</c> in the game, puts a marker line into the logs. Do something in the game, and everything logged after it is right under the marker. <cy>n</c> jumps between markers, warnings and errors.
- **Rebuilt mods**: when a `.geode` in the mods folder changes (`geode build` installs it), a banner says so and <cy>Shift+R</c> restarts the game.
- **Last crash**: if the last session crashed, a banner offers its crash log, <cy>o</c> opens it right in the console.
- **Time between logs**: <cy>t</c> shows the time since the log before, or since the group's start, to measure how long things take.
- **Log levels on the fly**: in the mods list <cy>+</c> and <cy>-</c> change how much a mod logs, <cy>l</c> turns its logging off, <cy>0</c> sets it back. They stay for the next launches.
- **Mod cards**: <cy>Enter</c> on a mod shows its version, hooks, patches and how many logs a second it writes. A mod writing 5 or more logs a second shows its rate in the list.
- **An API for your mod**: pin values in the console instead of logging them every frame (`improved_logger::watch("player x", x)`), time blocks of code (`improved_logger::Timer`), put markers from code (`improved_logger::mark("level started")`). No hard dependency: without Improved Logger the calls do nothing. See the README on GitHub.

## Both modes

- **Folds repeats**: a line written again and again stays one line with a counter (`×40`). In *Similar* mode lines that differ only in numbers fold too and show the latest values. Repeating blocks of up to 4 lines (like `Loading X` + `Loaded X`) fold as a whole.
- **Shows nesting**: logs inside `log::NestScope` get tree guides.
- **Pause gap**: after a pause, the next logs start a new group.
- **Focus and mute**: show everything from the mods you are working on, only warnings and errors from the rest.

The *Stream* mode prints the grouped lines as they come instead of the interactive screen.

Turn on <cy>Show Platform Console</c> in Geode's settings to see the console, the mod offers to on its first launch. Log files are left exactly as Geode writes them, every line is still there.
