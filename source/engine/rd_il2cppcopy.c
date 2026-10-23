/* rd_il2cppcopy.c -- see rd_il2cppcopy.h. MIT (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include "rd_game.h"
#include "rd_platform.h"
#include "rd_il2cppcopy.h"

#define STAMP_VERSION 1                 /* bump when what is copied or checked changes */

int rd_il2cppcopy_is_data_dir(const char *path) {
  static const char tail[] = "/" RD_IL2CPP_DATA_DIR;
  if (!path) return 0;
  const size_t n = strnlen(path, RD_PATH_MAX);
  if (n == RD_PATH_MAX || n < sizeof tail - 1) return 0;
  return !strcmp(path + n - (sizeof tail - 1), tail);
}

int rd_il2cppcopy_stamp(char *buf, size_t n, int64_t apk_size, int64_t apk_mtime) {
  const int k = snprintf(buf, n, "realdrift_nx il2cpp copy %d\nunity.build-id %s\n"
                         "base.apk %lld bytes, mtime %lld\n", STAMP_VERSION,
                         RD_GAME_UNITY_BUILD_ID, (long long)apk_size, (long long)apk_mtime);
  return k < 0 || (size_t)k >= n ? -1 : k;
}

int rd_il2cppcopy_current(const char *file, size_t len, const char *stamp, int64_t metadata_size) {
  if (!file || !stamp || metadata_size <= 0) return 0;
  return len == strlen(stamp) && !memcmp(file, stamp, len);
}
