/* rd_log.c -- see rd_log.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_log.h"

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

int rd_log_urgent(const char *line) {
  return line && (starts(line, RD_LOG_TAG_CRASH) || starts(line, RD_LOG_TAG_WD) ||
                  starts(line, RD_LOG_TAG_STDIO));
}

int rd_log_wanted(int level, const char *line) {
  if (!line) return 0;
  if (level != RD_LOG_MINIMAL) return 1;
  return rd_log_urgent(line) ||
         starts(line, RD_LOG_TAG_BANNER) ||
         starts(line, RD_LOG_TAG_GC) ||
         (starts(line, "Unity:") && strstr(line, "Exception"));
}

int rd_log_noisy(const char *fmt, int keep_jni) {
  if (!fmt) return 0;
  return starts(fmt, "dlsym") || starts(fmt, "dlopen") || starts(fmt, "JNI ") ||
         starts(fmt, "JNI:") || (!keep_jni && starts(fmt, RD_LOG_TAG_JNI));
}
