/* util.c -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "config.h"
#include "rd_log.h"
#include "rd_platform.h"

/* TPIDR_EL0, the user read/write thread pointer bionic code takes its TLS from. */
static inline void armSetTlsRw(void *addr) {
  __asm__ ("msr s3_3_c13_c0_2, %0" : : "r"(addr));
}

// File-only logger (DEBUG_LOG builds only): open once + flush per line so the tail survives a
// crash, mutex-serialised across engine threads. Drops the high-frequency dlsym/dlopen/JNI spam.
#if DEBUG_LOG
static Mutex g_log_lock;
#endif

#if DEBUG_LOG
static FILE *g_log_f = NULL;
#endif

/* Debug.log is fully buffered (64 KB) instead of fflush-per-line (an SD write per
 * line, ~960 lines per track load). Crash/stall lines flush at once (rd_log_urgent); the rest
 * is flushed ~1/s by debug_log_flush() from the clock thread and at exit. debug_log=0 in
 * config.txt keeps only the important lines (rd_log_wanted).
 *
 * The filters look at the rendered line, not the format: diag.c passes its tag as "%s", so a
 * format-only check took its "[crash]" report for an ordinary line, and an abort() lost it in
 * the buffer (debug.log 2026-11-03). A line longer than RD_LOG_LINE_BYTES is printed again
 * from the format, whole. */
int debugPrintf(char *text, ...) {
#if DEBUG_LOG
  va_list list;
  if (rd_log_noisy(text, RD_LOG_JNI)) return 0;
  char line[RD_LOG_LINE_BYTES];
  va_start(list, text);
  const int n = vsnprintf(line, sizeof line, text, list);
  va_end(list);
  if (n < 0 || !rd_log_wanted(config.debug_log, line)) return 0;
  mutexLock(&g_log_lock);
  if (!g_log_f) {
    g_log_f = fopen(RD_LOG_PATH, "a");
    if (g_log_f) setvbuf(g_log_f, NULL, _IOFBF, RD_LOG_BUFFER_BYTES);
  }
  if (g_log_f) {
    if ((size_t)n < sizeof line) {
      fwrite(line, 1, (size_t)n, g_log_f);
    } else {
      va_start(list, text); vfprintf(g_log_f, text, list); va_end(list);
    }
    if (rd_log_urgent(line)) fflush(g_log_f);
  }
  mutexUnlock(&g_log_lock);
#else
  (void)text;
#endif
  return 0;
}

void debug_log_flush(void) {
#if DEBUG_LOG
  if (!mutexTryLock(&g_log_lock)) return;   /* never block the caller on the log */
  if (g_log_f) fflush(g_log_f);
  mutexUnlock(&g_log_lock);
#endif
}

// Per-thread bionic TLS. The engine reads its stack canary from tpidr_el0+0x28;
// every thread that runs engine code needs its OWN zeroed block here. A single
// shared block races: one thread's TLS writes (including the guard slot) corrupt
// another thread's in-flight canary, tripping a false __stack_chk_fail. `buf`
// must outlive the thread (TPIDR_EL0 points into it until the thread exits).
void install_bionic_tls(void *buf) {
  memset(buf, 0, BIONIC_TLS_SIZE);
  armSetTlsRw((uint8_t *)buf + BIONIC_TLS_TP_OFFSET);
}

// CPU boost while loading (ApmCpuBoostMode_FastLoad, RD_BOOST_CPU_MHZ)
void cpu_boost(int on) {
  appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

