# Improved Logger

Makes the platform console readable when many mods log at once, or one mod logs a lot.

## Interactive console

The console becomes a screen to browse the logs:

- **Groups**: logs that come one after another from the same mod sit in one group with its name, count, warnings and errors. Fold and unfold them with <cy>Space</c>, all at once with <cy>c</c> and <cy>e</c>.
- **Search**: <cy>/</c> filters as you type and marks the matches.
- **Levels**: <cy>1</c> to <cy>5</c> show from TRACE, DEBUG, INFO, WARN or ERROR up.
- **Mods**: <cy>Tab</c> opens the list of mods with their counts. Hide a mod, or show only it.
- **Details**: <cy>Enter</c> opens a log in full, <cy>y</c> or <cy>Ctrl+C</c> copies it.
- **Live or paused**: scrolling up pauses the view, <cy>End</c> goes back to the new logs.
- **Mouse**: the wheel scrolls, a click selects or folds, a double click opens a log.

Press <cy>?</c> in the console for every key.

## Both modes

- **Folds repeats**: a line written again and again stays one line with a counter (`×40`). In *Similar* mode lines that differ only in numbers fold too and show the latest values. Repeating blocks of up to 4 lines (like `Loading X` + `Loaded X`) fold as a whole.
- **Shows nesting**: logs inside `log::NestScope` get tree guides.
- **Pause gap**: after a pause, the next logs start a new group.
- **Focus and mute**: show everything from the mods you are working on, only warnings and errors from the rest.

The *Stream* mode prints the grouped lines as they come instead of the interactive screen.

Turn on <cy>Show Platform Console</c> in Geode's settings to see the console. Log files are left exactly as Geode writes them, every line is still there.
