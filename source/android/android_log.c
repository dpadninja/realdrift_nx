/* android_log.c -- liblog (__android_log_*) and bionic's __assert2: the engine's Android log goes
 * to debug.log. Split out of imports.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


#include "config.h"
#include "util.h"
#include "android_log.h"

// ---------------------------------------------------------------------------
// liblog
// ---------------------------------------------------------------------------

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
#if DEBUG_LOG
  va_list list; static char string[0x1000];
  va_start(list, fmt); vsnprintf(string, sizeof(string), fmt, list); va_end(list);
  debugPrintf("%s: %s\n", tag, string);
#else
  (void)tag; (void)fmt;
#endif
  return 0;
}
int __android_log_write(int prio, const char *tag, const char *text) {
  (void)prio; debugPrintf("%s: %s\n", tag, text); return 0;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list va) {
  (void)prio;
#if DEBUG_LOG
  static char string[0x1000]; vsnprintf(string, sizeof(string), fmt, va);
  debugPrintf("%s: %s\n", tag, string);
#else
  (void)tag; (void)fmt; (void)va;
#endif
  return 0;
}
void __assert2(const char *file, int line, const char *func, const char *expr) {
  debugPrintf("assert: %s:%d (%s): %s\n", file, line, func, expr); abort();
}
