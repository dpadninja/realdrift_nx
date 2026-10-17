/* Minimal host-side test harness: each test is a function; CHECK records failures.
 * MIT (see LICENSE). */
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond) do {                                                     \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n",              \
                           __FILE__, __LINE__, #cond); g_failures++; }       \
  } while (0)

#define RUN(test) do { test(); } while (0)

#endif
