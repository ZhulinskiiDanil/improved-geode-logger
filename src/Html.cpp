#include "Html.hpp"
#include "Levels.hpp"

#include <array>
#include <cstdio>

namespace html
{
  std::string escape(std::string_view text)
  {
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
      switch (c)
      {
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '&':
        out += "&amp;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
      }
    }
    return out;
  }

  std::string color(int index)
  {
    static constexpr std::array<int, 6> cube = {0, 95, 135, 175, 215, 255};
    static constexpr std::array<int, 16> basic = {
        0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
        0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff};

    int rgb = 0xc0c0c0;
    if (index >= 0 && index < 16)
      rgb = basic[index];
    else if (index >= 16 && index < 232)
    {
      int i = index - 16;
      rgb = (cube[i / 36] << 16) | (cube[i / 6 % 6] << 8) | cube[i % 6];
    }
    else if (index >= 232 && index < 256)
    {
      int gray = 8 + (index - 232) * 10;
      rgb = (gray << 16) | (gray << 8) | gray;
    }

    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%06x", rgb);
    return buffer;
  }

  static std::string_view levelClass(int severity)
  {
    switch (severity)
    {
    case level::Trace:
      return "trace";
    case level::Debug:
      return "debug";
    case level::Warning:
      return "warn";
    case level::Error:
      return "error";
    default:
      return "info";
    }
  }

  static std::string_view levelName(int severity)
  {
    switch (severity)
    {
    case level::Trace:
      return "TRACE";
    case level::Debug:
      return "DEBUG";
    case level::Warning:
      return "WARN";
    case level::Error:
      return "ERROR";
    default:
      return "INFO";
    }
  }

  static constexpr std::string_view STYLE = R"(
body { background: #111318; color: #d8dbe2; font: 13px/1.45 Consolas, "Cascadia Mono", monospace; margin: 0; }
header { position: sticky; top: 0; background: #1b2030; padding: 10px 16px; display: flex; gap: 16px; align-items: center; }
header h1 { font-size: 14px; margin: 0; color: #fff; }
header input { flex: 1; background: #111318; color: #fff; border: 1px solid #39405a; border-radius: 4px; padding: 5px 8px; font: inherit; }
header span { color: #8a90a2; }
main { padding: 8px 16px 40px; }
details { margin: 2px 0; }
summary { cursor: pointer; font-weight: bold; padding: 2px 0; }
summary .meta { font-weight: normal; color: #8a90a2; margin-left: 10px; }
.row { white-space: pre-wrap; padding-left: 14px; border-left: 2px solid var(--mod); margin-left: 4px; }
.time { color: #767676; }
.level { display: inline-block; width: 6ch; }
.trace .level { color: #5f0087; } .debug .level { color: #767676; } .info .level { color: #00d7ff; }
.warn .level { color: #ffffaf; font-weight: bold; } .error .level { color: #ff5f5f; font-weight: bold; }
.warn .text { color: #ffffd7; } .error .text { color: #ffd7d7; } .debug .text { color: #bcbcbc; }
.thread { color: #585858; }
.count { background: var(--mod); color: #000; border-radius: 3px; padding: 0 5px; margin-left: 8px; font-weight: bold; }
.warnings { color: #ffd75f; } .errors { color: #ff5f5f; }
.marker { color: #ff87ff; font-weight: bold; margin: 8px 0; border-top: 2px solid #ff87ff; padding-top: 2px; }
.hidden { display: none; }
)";

  // Hides the lines the box doesn't match, and the groups left empty
  static constexpr std::string_view SCRIPT = R"(
const box = document.querySelector('header input');
box.addEventListener('input', () => {
  const words = box.value.toLowerCase().split(' ').filter(w => w);
  for (const group of document.querySelectorAll('details')) {
    let any = false;
    for (const row of group.querySelectorAll('.row')) {
      const text = row.textContent.toLowerCase();
      const shown = words.every(w => w.startsWith('-') && w.length > 1 ? !text.includes(w.slice(1)) : text.includes(w));
      row.classList.toggle('hidden', !shown);
      any ||= shown;
    }
    group.classList.toggle('hidden', !any && words.length > 0);
    if (words.length) group.open = any;
  }
});
)";

  std::string render(model::Model const& model, Options const& options)
  {
    std::string body;
    int logs = 0;

    for (auto const& group : model.groups)
    {
      if (options.keepGroup && !options.keepGroup(group))
        continue;

      if (group.marker)
      {
        auto const& marker = group.rows.front().record;
        body += "<div class=\"marker\">" + escape(marker.content) + " · " + escape(marker.time) + "</div>\n";
        continue;
      }

      std::string rows;
      for (auto const& row : group.rows)
      {
        auto const& record = row.record;
        if (options.keepRow && !options.keepRow(record))
          continue;
        logs++;

        rows += "<div class=\"row " + std::string(levelClass(record.severity)) + "\">";
        rows += "<span class=\"time\">" + escape(record.time) + "</span> ";
        rows += "<span class=\"level\">" + std::string(levelName(record.severity)) + "</span>";
        if (!record.source.thread.empty() && record.source.thread != "Main")
          rows += "<span class=\"thread\">[" + escape(record.source.thread) + "]</span> ";
        rows += "<span class=\"text\">" + std::string(record.nest * 2, ' ') + escape(record.content) + "</span>";
        if (row.count > 1)
          rows += "<span class=\"count\">×" + std::to_string(row.count) + "</span>";
        rows += "</div>\n";
      }
      if (rows.empty())
        continue;

      body += "<details open style=\"--mod: " + color(group.color) + "\"><summary style=\"color: " + color(group.color) + "\">";
      body += escape(group.source.name);
      body += "<span class=\"meta\">" + std::to_string(group.logs) + (group.logs == 1 ? " log" : " logs");
      if (group.warnings)
        body += " <span class=\"warnings\">⚠ " + std::to_string(group.warnings) + "</span>";
      if (group.errors)
        body += " <span class=\"errors\">✖ " + std::to_string(group.errors) + "</span>";
      body += " · " + escape(group.firstTime) + (group.firstTime == group.lastTime ? "" : " – " + escape(group.lastTime));
      body += "</span></summary>\n" + rows + "</details>\n";
    }

    std::string page = "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>" + escape(options.title) + "</title>";
    page += "<style>" + std::string(STYLE) + "</style></head><body>\n";
    page += "<header><h1>" + escape(options.title) + "</h1><input placeholder=\"Filter: words, -word to leave out\"><span>" +
            std::to_string(logs) + " logs</span></header>\n<main>\n" + body + "</main>\n";
    page += "<script>" + std::string(SCRIPT) + "</script></body></html>\n";
    return page;
  }
}
