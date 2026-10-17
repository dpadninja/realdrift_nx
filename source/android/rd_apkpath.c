/* rd_apkpath.c -- see rd_apkpath.h. MIT (see LICENSE). */
#include <stdio.h>
#include "rd_apkpath.h"

const char *rd_package_code_path(const char *root, int (*exists)(const char *path),
                                 char *buf, size_t n) {
  if (!root || !buf) return NULL;
  int len = snprintf(buf, n, "%s/%s", root, RD_BASE_APK_NAME);
  if (len >= 0 && (size_t)len < n && exists && exists(buf)) return buf;
  len = snprintf(buf, n, "%s", root);
  return (len >= 0 && (size_t)len < n) ? buf : NULL;
}
