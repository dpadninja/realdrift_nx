/* sources: source/game/rd_player.c */
/* Host unit test of rd_player. MIT (see LICENSE). */
#include "check.h"
#include "rd_player.h"

/* The default country replaces only what the player never chose: no key, an empty one, or
 * Andorra, which the game stores when its web lookup fails. A picked flag stays. */
static void test_country_unset(void) {
  CHECK(rd_player_country_unset(NULL));
  CHECK(rd_player_country_unset(""));
  CHECK(rd_player_country_unset("AD"));
  CHECK(!rd_player_country_unset("RU"));
  CHECK(!rd_player_country_unset("JP"));
}

int main(void) {
  RUN(test_country_unset);
  return g_failures != 0;
}
