// Tests for src/LogLine.cpp, the lines of Geode's log files
#include "../src/Levels.hpp"
#include "../src/LogLine.hpp"
#include "Check.hpp"

static void testFullLine()
{
  auto line = logline::parse("13:38:54.123 DEBUG [Main] [Geode]: Loading binary");
  CHECK(line.has_value());
  if (!line)
    return;
  CHECK(line->time == "13:38:54.123");
  CHECK(line->millis == ((13 * 60 + 38) * 60 + 54) * 1000LL + 123);
  CHECK(line->severity == level::Debug);
  CHECK(line->thread == "Main");
  CHECK(line->source == "Geode");
  CHECK(line->nest == 0);
  CHECK(line->content == "Loading binary");
}

static void testWithoutThreadOrMillis()
{
  // Logs from stdout have no thread
  auto line = logline::parse("09:05:01 INFO  [stdout]: hello");
  CHECK(line.has_value());
  if (!line)
    return;
  CHECK(line->time == "09:05:01");
  CHECK(line->millis == ((9 * 60 + 5) * 60 + 1) * 1000LL);
  CHECK(line->severity == level::Info);
  CHECK(line->thread.empty());
  CHECK(line->source == "stdout");
  CHECK(line->content == "hello");
}

static void testLevelsAndNesting()
{
  CHECK(logline::parse("10:00:00 WARN  [Main] [Geode]: x")->severity == level::Warning);
  CHECK(logline::parse("10:00:00 ERROR [Main] [Geode]: x")->severity == level::Error);
  CHECK(logline::parse("10:00:00 TRACE [Main] [Geode]: x")->severity == level::Trace);

  // Two spaces a level of NestScope
  auto nested = logline::parse("10:00:00 INFO  [Main] [Geode]:     Loading mod");
  CHECK(nested && nested->nest == 2 && nested->content == "Loading mod");
}

static void testOddNames()
{
  // Geode cuts long names with ">" when the nesting runs out of room
  auto line = logline::parse("10:00:00 INFO  [M>] [Improved L>]: x");
  CHECK(line && line->thread == "M>" && line->source == "Improved L>");

  // Brackets in the message stay in it
  auto brackets = logline::parse("10:00:00 INFO  [Main] [Mega Hack]: [CORE] Config loaded");
  CHECK(brackets && brackets->content == "[CORE] Config loaded");
}

static void testNotALogStart()
{
  // The second line of a multi-line log, or anything else
  CHECK(!logline::parse("  at foo()"));
  CHECK(!logline::parse(""));
  CHECK(!logline::parse("1x:00:00 INFO  [Main] [Geode]: x"));
  CHECK(!logline::parse("10:00:00 INFO  Geode: no brackets"));
  CHECK(!logline::parse("10:00:00 INFO  [Main] [Geode] missing colon"));
}

int main()
{
  testFullLine();
  testWithoutThreadOrMillis();
  testLevelsAndNesting();
  testOddNames();
  testNotALogStart();
  return finish("log lines");
}
