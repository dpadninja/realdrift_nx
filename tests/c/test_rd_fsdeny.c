/* sources: source/bionic/rd_fsdeny.c */
/* Host unit test of rd_fsdeny (files the port refuses to write). MIT (see LICENSE). */
#include "check.h"
#include "rd_fsdeny.h"
#include "rd_game.h"

#define ANALYTICS RD_GAME_HOME_DIR "/Unity/3c664128-dfd4-432a-afbd-6287debc9a56/Analytics"

/* The paths of the console log (2026-10-27) are refused once the refusal is on: the folder
 * itself (mkdir), its values file and the archived events, with or without "sdmc:". */
static void test_analytics_refused(void) {
  rd_fsdeny_set_analytics(1);
  CHECK(rd_fsdeny_write(ANALYTICS));
  CHECK(rd_fsdeny_write(ANALYTICS "/values"));
  CHECK(rd_fsdeny_write(ANALYTICS "/ArchivedEvents/179311234700002.aeb0e9db/s"));
  CHECK(rd_fsdeny_write("sdmc:" ANALYTICS "/ArchivedEvents"));
}

/* Everything else the game writes stays writable: the saves, the il2cpp data, Unity's other
 * per-project files, a folder that only starts with "Analytics", and paths outside the game
 * folder. A wrong match here would lose saves, so the rule is exact. */
static void test_others_allowed(void) {
  rd_fsdeny_set_analytics(1);
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/prefs.kv"));
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/il2cpp/Metadata/global-metadata.dat"));
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/Unity/3c66/UnityConnect"));
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/Unity/3c66/AnalyticsX/values"));
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/Unity//Analytics/values"));
  CHECK(!rd_fsdeny_write(RD_GAME_HOME_DIR "/Unity/Analytics"));
  CHECK(!rd_fsdeny_write("/switch/other/Unity/3c66/Analytics/values"));
  CHECK(!rd_fsdeny_write(NULL));
}

/* Off (offline_ui = 0, and the default before boot sets it) nothing is refused: the game
 * behaves as before. */
static void test_off(void) {
  rd_fsdeny_set_analytics(0);
  CHECK(!rd_fsdeny_write(ANALYTICS "/values"));
}

int main(void) {
  RUN(test_analytics_refused);
  RUN(test_others_allowed);
  RUN(test_off);
  return g_failures != 0;
}
