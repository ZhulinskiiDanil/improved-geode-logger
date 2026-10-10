#pragma once

// A tiny check for the tests: no framework, just a count of what failed
#include <cstdio>

inline int g_failures = 0;

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
      g_failures++;                                                          \
    }                                                                        \
  } while (false)

inline int finish(char const* name)
{
  if (g_failures)
  {
    std::printf("%s: %d failed\n", name, g_failures);
    return 1;
  }
  std::printf("%s: all passed\n", name);
  return 0;
}
