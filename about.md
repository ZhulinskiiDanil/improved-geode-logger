# Improved Logger

Makes the platform console readable when many mods log at once, or one mod logs a lot.

- **Groups by mod**: logs that come one after another from the same mod go under one colored header instead of repeating `[Main] [Mod]` on every line. Another thread gets its own header.
- **Folds repeats**: a line written again and again stays one line with a counter (`×40`). In *Similar* mode lines that differ only in numbers fold too and show the latest values. Repeating blocks of up to 4 lines (like `Loading X` + `Loaded X`) fold as a whole.
- **Shows nesting**: logs inside `log::NestScope` get tree guides.
- **Pause gap**: after a pause, the next burst of logs starts after an empty line.
- **Focus and mute**: show everything from the mods you are working on, only warnings and errors from the rest.

Turn on <cy>Show Platform Console</c> in Geode's settings to see the console. Log files are left exactly as Geode writes them, every line is still there.
