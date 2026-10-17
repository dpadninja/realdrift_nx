/* rd_log.h -- what debug.log keeps and what must hit the SD card at once.
 * Host-testable (tests/c/test_rd_log.c). Used by debugPrintf (util.c).
 *
 * debug.log used to fflush every line (an SD write each, ~960 lines per track load, run 13).
 * It is now fully buffered and flushed about once a second by the clock thread, except the
 * "urgent" lines below, which flush immediately so a crash/stall report is never lost.
 * MIT (see LICENSE). */
#ifndef RD_LOG_H
#define RD_LOG_H

enum { RD_LOG_MINIMAL = 0, RD_LOG_FULL = 1 };   /* config.txt debug_log=0|1 */

/* Line prefixes the filter below looks at. Producers use the same macros
 * (debugPrintf(RD_LOG_TAG_GC " ...")), so renaming a tag cannot silently break filtering. */
#define RD_LOG_TAG_CRASH  "[crash]"      /* crash handler: always kept, flushed at once */
#define RD_LOG_TAG_WD     "[wd]"         /* stall watchdog: always kept, flushed at once */
#define RD_LOG_TAG_STDIO  "stdio:"       /* the engine's own stdout/stderr: flushed at once */
#define RD_LOG_TAG_GC     "[gc]"         /* GC bridge: kept at debug_log=0 */
#define RD_LOG_TAG_BANNER "[boot] ==="   /* start banner + build stamp: kept at debug_log=0 */
#define RD_LOG_TAG_JNI    "[jni]"        /* per-method JNI trace: only if RD_LOG_JNI */
#define RD_LOG_TAG_MODS   "[mods]"       /* the mod host: kept at debug_log=0 */
#define RD_LOG_TAG_MOD    "[mod:"        /* a mod's own lines ("[mod:<id>] ..."): kept too */

/* 1 if a line starting with `fmt` is written at `level`. */
int rd_log_wanted(int level, const char *fmt);

/* 1 for the high-frequency loader chatter that is never logged (dlsym/dlopen/JNI lookups;
 * the "[jni]" trace too unless keep_jni). */
int rd_log_noisy(const char *fmt, int keep_jni);

/* 1 if a line starting with `fmt` must be flushed to the SD card right away. */
int rd_log_urgent(const char *fmt);

#endif
