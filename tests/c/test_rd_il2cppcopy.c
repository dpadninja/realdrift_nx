/* sources: source/engine/rd_il2cppcopy.c */
/* Host unit test of rd_il2cppcopy (fast_boot: the il2cpp data copied once). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_game.h"
#include "rd_platform.h"
#include "rd_il2cppcopy.h"

/* Only the copy's destination folder is taken over; anything else Unity copies goes on as
 * before, including paths that merely contain the name. */
static void test_is_data_dir(void) {
  CHECK(rd_il2cppcopy_is_data_dir(RD_GAME_HOME_DIR "/" RD_IL2CPP_DATA_DIR));
  CHECK(rd_il2cppcopy_is_data_dir("/data/files/il2cpp"));
  CHECK(!rd_il2cppcopy_is_data_dir(RD_GAME_HOME_DIR "/il2cpp/Metadata"));
  CHECK(!rd_il2cppcopy_is_data_dir(RD_GAME_HOME_DIR "/xil2cpp"));
  CHECK(!rd_il2cppcopy_is_data_dir("il2cpp"));
  CHECK(!rd_il2cppcopy_is_data_dir(NULL));
  static char unterminated[RD_PATH_MAX];                 /* garbage: no NUL in range */
  memset(unterminated, 'a', sizeof unterminated);
  CHECK(!rd_il2cppcopy_is_data_dir(unterminated));
}

/* The stamp names the build-id and the APK's size and time, so another APK gives another
 * stamp; a buffer too small is an error, not a cut stamp. */
static void test_stamp(void) {
  char a[RD_IL2CPPCOPY_STAMP_MAX], b[RD_IL2CPPCOPY_STAMP_MAX];
  const int n = rd_il2cppcopy_stamp(a, sizeof a, 53545581, 1760000000);
  CHECK(n > 0 && (size_t)n == strlen(a));
  CHECK(strstr(a, RD_GAME_UNITY_BUILD_ID) && strstr(a, "53545581") && strstr(a, "1760000000"));
  rd_il2cppcopy_stamp(b, sizeof b, 53545581, 1760000001);
  CHECK(strcmp(a, b) != 0);
  rd_il2cppcopy_stamp(b, sizeof b, 53545580, 1760000000);
  CHECK(strcmp(a, b) != 0);
  CHECK(rd_il2cppcopy_stamp(b, 10, 1, 1) == -1);
}

/* Skip only with the same stamp and the metadata in place: a missing, truncated, extended or
 * different stamp, or no global-metadata.dat, means Unity copies again. */
static void test_current(void) {
  char s[RD_IL2CPPCOPY_STAMP_MAX];
  const int n = rd_il2cppcopy_stamp(s, sizeof s, 100, 200);
  CHECK(rd_il2cppcopy_current(s, (size_t)n, s, 3000000));
  CHECK(!rd_il2cppcopy_current(s, (size_t)n, s, 0));
  CHECK(!rd_il2cppcopy_current(s, (size_t)n - 1, s, 3000000));
  CHECK(!rd_il2cppcopy_current(s, 0, s, 3000000));
  char longer[RD_IL2CPPCOPY_STAMP_MAX + 1];
  memcpy(longer, s, (size_t)n);
  longer[n] = 'x';
  CHECK(!rd_il2cppcopy_current(longer, (size_t)n + 1, s, 3000000));
  char other[RD_IL2CPPCOPY_STAMP_MAX];
  rd_il2cppcopy_stamp(other, sizeof other, 101, 200);
  CHECK(!rd_il2cppcopy_current(other, strlen(other), s, 3000000));
  CHECK(!rd_il2cppcopy_current(NULL, 0, s, 1));
}

int main(void) {
  RUN(test_is_data_dir);
  RUN(test_stamp);
  RUN(test_current);
  return g_failures != 0;
}
