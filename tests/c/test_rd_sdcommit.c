/* sources: source/bionic/rd_sdcommit.c */
/* Host unit test of rd_sdcommit (which closed files commit the SD card). MIT (see LICENSE). */
#include "check.h"
#include "rd_game.h"
#include "rd_sdcommit.h"

/* Unity's per-launch il2cpp copy waits for the periodic flush, with or without the device
 * prefix; saves and everything else are committed on close. */
static void test_on_close(void) {
  CHECK(!rd_sdcommit_on_close(RD_GAME_HOME_DIR "/il2cpp/Metadata/global-metadata.dat"));
  CHECK(!rd_sdcommit_on_close(RD_GAME_HOME "/il2cpp/etc/mono/4.5/machine.config"));
  CHECK(rd_sdcommit_on_close(RD_GAME_HOME_DIR "/" RD_PREFS_FILE));
  CHECK(rd_sdcommit_on_close(RD_GAME_HOME_DIR "/Unity/x/Analytics/values"));
  CHECK(rd_sdcommit_on_close(RD_GAME_HOME_DIR "/il2cpp"));               /* not inside it */
  CHECK(rd_sdcommit_on_close(RD_GAME_HOME_DIR "/il2cppx/a"));
  CHECK(rd_sdcommit_on_close("/other/il2cpp/a"));
  CHECK(rd_sdcommit_on_close(NULL));
}

int main(void) {
  RUN(test_on_close);
  return g_failures != 0;
}
