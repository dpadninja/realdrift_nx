/* sources: source/game/rd_racemusic.c */
/* Host unit test of rd_racemusic (the preloaded track names). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_racemusic.h"

/* A new name is added once; the same name in the next race's Awake is already held, so the
 * clip is not loaded and held twice. */
static void test_add_once(void) {
  static rd_music_cache c;
  CHECK(rd_music_cache_add(&c, "Music/Track1") == 1);
  CHECK(rd_music_cache_add(&c, "Music/Track2") == 1);
  CHECK(rd_music_cache_add(&c, "Music/Track1") == 0);
  CHECK(c.n == 2 && !strcmp(c.names[1], "Music/Track2"));
}

/* Names that cannot be kept are refused instead of truncated (a truncated name could match
 * another track): empty, NULL, too long, or a full set. */
static void test_refused(void) {
  static rd_music_cache c;
  char longname[RD_MUSIC_NAME_MAX + 1];
  memset(longname, 'a', sizeof longname - 1);
  longname[sizeof longname - 1] = 0;
  CHECK(rd_music_cache_add(&c, "") == -1);
  CHECK(rd_music_cache_add(&c, NULL) == -1);
  CHECK(rd_music_cache_add(&c, longname) == -1);
  longname[RD_MUSIC_NAME_MAX - 1] = 0;                 /* the longest that fits */
  CHECK(rd_music_cache_add(&c, longname) == 1);
  char name[8];
  for (int i = 1; i < RD_MUSIC_MAX; i++) {
    name[0] = 't'; name[1] = (char)('0' + i / 10); name[2] = (char)('0' + i % 10); name[3] = 0;
    CHECK(rd_music_cache_add(&c, name) == 1);
  }
  CHECK(rd_music_cache_add(&c, "one more") == -1);
}

int main(void) {
  RUN(test_add_once);
  RUN(test_refused);
  return g_failures != 0;
}
