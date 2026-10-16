/* rd_log.c -- see rd_log.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_log.h"

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

int rd_log_urgent(const char *fmt) {
  return fmt && (starts(fmt, RD_LOG_TAG_CRASH) || starts(fmt, RD_LOG_TAG_WD) ||
                 starts(fmt, RD_LOG_TAG_STDIO));
}

int rd_log_wanted(int level, const char *fmt) {
  if (!fmt) return 0;
  if (level != RD_LOG_MINIMAL) return 1;
  return rd_log_urgent(fmt) ||
         starts(fmt, RD_LOG_TAG_BANNER) ||
         starts(fmt, RD_LOG_TAG_GC) ||
         (starts(fmt, "Unity:") && strstr(fmt, "Exception"));
}

int rd_log_noisy(const char *fmt, int keep_jni) {
  if (!fmt) return 0;
  return starts(fmt, "dlsym") || starts(fmt, "dlopen") || starts(fmt, "JNI ") ||
         starts(fmt, "JNI:") || (!keep_jni && starts(fmt, RD_LOG_TAG_JNI));
}
