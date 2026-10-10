// Tests for src/Html.cpp, the page to share
#include "../src/Html.hpp"
#include "../src/Levels.hpp"
#include "Check.hpp"

static bool has(std::string const& page, std::string_view part)
{
  return page.find(part) != std::string::npos;
}

static void testEscape()
{
  CHECK(html::escape("<script>alert(\"x\") & more</script>") == "&lt;script&gt;alert(&quot;x&quot;) &amp; more&lt;/script&gt;");
  CHECK(html::escape("plain") == "plain");
}

static void testColors()
{
  CHECK(html::color(16) == "#000000");
  CHECK(html::color(231) == "#ffffff");
  CHECK(html::color(196) == "#ff0000");
  CHECK(html::color(232) == "#080808");
  CHECK(html::color(9) == "#ff0000");
}

static void testPage()
{
  model::Model model;
  settings::Config config;
  model.add(Record{.source = {nullptr, "Icon <Mayhem>", "Main", ""}, .severity = level::Error, .time = "12:00:00", .content = "broke <b>"}, config);
  model.add(Record{.source = {nullptr, "Other", "Worker", ""}, .severity = level::Debug, .time = "12:00:01", .content = "hidden one"}, config);

  auto page = html::render(model, {.title = "Logs & more"});
  CHECK(has(page, "<!doctype html>"));
  CHECK(has(page, "<title>Logs &amp; more</title>"));
  // Names and messages are escaped, never markup
  CHECK(has(page, "Icon &lt;Mayhem&gt;"));
  CHECK(has(page, "broke &lt;b&gt;"));
  CHECK(!has(page, "broke <b>"));
  CHECK(has(page, "class=\"row error\""));
  CHECK(has(page, "[Worker]"));
  CHECK(has(page, "2 logs</span></header>"));

  // The filters leave groups and rows out
  auto filtered = html::render(model, {.title = "x", .keepGroup = [](auto const& group)
                                                    { return group.source.name != "Other"; }});
  CHECK(!has(filtered, "hidden one"));
  CHECK(has(filtered, "1 logs</span></header>"));
}

int main()
{
  testEscape();
  testColors();
  testPage();
  return finish("html");
}
