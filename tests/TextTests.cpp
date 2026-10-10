// Tests for src/Text.cpp: it has no Geode in it, so it builds and runs on its own
#include "../src/Text.hpp"

#include <cstdio>

static int s_failures = 0;

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
      s_failures++;                                                          \
    }                                                                        \
  } while (false)

// ! --- Numbers --- !

static void testWithoutNumbers()
{
  // Lines that differ only in numbers fold together
  CHECK(text::withoutNumbers("pos 12.5, -3") == text::withoutNumbers("pos 13, -2.25"));
  CHECK(text::withoutNumbers("Loading 3/10") == "Loading #/#");
  CHECK(text::withoutNumbers("ptr 0x7ff6014d55c0 done") == "ptr # done");
  // The dot at the end of a sentence stays
  CHECK(text::withoutNumbers("took 12.") == "took #.");
  // Words with digits in them still change
  CHECK(text::withoutNumbers("player_101") == "player_#");
  CHECK(text::withoutNumbers("no numbers here") == "no numbers here");
  CHECK(text::withoutNumbers("") == "");
  // A minus before a number belongs to it, a dash between words doesn't
  CHECK(text::withoutNumbers("a - b") == "a - b");
  CHECK(text::withoutNumbers("x=-5") == "x=#");
}

// ! --- Width --- !

static void testWidth()
{
  CHECK(text::width("hello") == 5);
  CHECK(text::width("") == 0);
  // Cyrillic is one column a letter
  CHECK(text::width("привет") == 6);
  // CJK and emoji take two
  CHECK(text::width("日本") == 4);
  CHECK(text::width("😀") == 2);
  CHECK(text::width("a\tb") == 6);
  // Control characters take nothing
  CHECK(text::width("a\x01" "b") == 2);
  CHECK(text::width("│ ") == 2);
}

static void testFit()
{
  CHECK(text::fit("hello", 10) == "hello");
  CHECK(text::fit("hello", 3) == "hel");
  CHECK(text::fit("hello", 0) == "");
  // Never cuts a character in half
  CHECK(text::fit("привет", 3) == "при");
  CHECK(text::fit("日本語", 3) == "日");
  CHECK(text::fit("日本語", 4) == "日本");
}

// ! --- Lines --- !

static void testSplitLines()
{
  auto lines = text::splitLines("a\nb\r\nc");
  CHECK(lines.size() == 3);
  CHECK(lines.size() == 3 && lines[0] == "a" && lines[1] == "b" && lines[2] == "c");

  // A log ending with a newline has no empty line at the end
  CHECK(text::splitLines("one\n").size() == 1);
  CHECK(text::splitLines("").size() == 1);
  // Empty lines in the middle stay
  CHECK(text::splitLines("a\n\nb").size() == 3);
}

static void testWrap()
{
  auto lines = text::wrap("abcdefgh", 3);
  CHECK(lines.size() == 3);
  CHECK(lines.size() == 3 && lines[0] == "abc" && lines[1] == "def" && lines[2] == "gh");

  CHECK(text::wrap("short", 10).size() == 1);
  CHECK(text::wrap("a\nb", 10).size() == 2);
  // A character wider than the line still goes somewhere instead of looping forever
  CHECK(text::wrap("日本", 1).size() == 2);
  CHECK(text::wrap("", 5).size() == 1);
}

static void testClean()
{
  CHECK(text::clean("\x1b[38;5;45mcolored\x1b[0m") == "colored");
  CHECK(text::clean("a\tb") == "a    b");
  CHECK(text::clean("line\nnext") == "line\nnext");
  CHECK(text::clean("bell\x07") == "bell");
  CHECK(text::clean("привет") == "привет");
}

// ! --- Search --- !

static void testCaseless()
{
  CHECK(text::containsCaseless("Loading Mod", "mod"));
  CHECK(text::containsCaseless("anything", ""));
  CHECK(!text::containsCaseless("abc", "abcd"));
  CHECK(text::findCaseless("Hook hook HOOK", "hook") == 0);
  CHECK(text::findCaseless("Hook hook HOOK", "hook", 1) == 5);
  CHECK(text::findCaseless("Hook hook HOOK", "hook", 6) == 10);
  CHECK(text::findCaseless("abc", "x") == std::string_view::npos);
}

static void testColorFor()
{
  // The same mod keeps its color
  CHECK(text::colorFor("geode.loader") == text::colorFor("geode.loader"));
  CHECK(text::colorFor("") > 0);
}

int main()
{
  testWithoutNumbers();
  testWidth();
  testFit();
  testSplitLines();
  testWrap();
  testClean();
  testCaseless();
  testColorFor();

  if (s_failures)
  {
    std::printf("%d failed\n", s_failures);
    return 1;
  }
  std::printf("All text tests passed\n");
  return 0;
}
