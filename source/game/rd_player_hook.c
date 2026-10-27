/* rd_player_hook.c -- the default flag at boot, glue (see rd_player.h).
 * MIT (see LICENSE). */
#include "rd_player.h"
#include "rd_game_symbols.h"
#include "unity_jni.h"
#include "config.h"
#include "util.h"

void rd_player_boot(void) {
  const char *stored = unity_prefs_get_string(RD_PREF_COUNTRY);
  if (config.country[0] && rd_player_country_unset(stored)) {
    unity_prefs_put_string(RD_PREF_COUNTRY, config.country);
    debugPrintf("[profile] country %s (was %s)\n", config.country, stored ? stored : "unset");
  }
}
