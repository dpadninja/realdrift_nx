/* rd_player_hook.c -- player name and country at boot, glue (see rd_player.h).
 * MIT (see LICENSE). */
#include "rd_player.h"
#include "rd_profile.h"
#include "rd_game_symbols.h"
#include "unity_jni.h"
#include "config.h"
#include "util.h"

void rd_player_boot(void) {
  if (config.switch_name) {
    char nick[RD_PROFILE_NICK_MAX + 1], name[RD_PROFILE_NICK_MAX + 1];
    if (rd_profile_nickname(nick, sizeof nick) && rd_player_name(nick, name, sizeof name)) {
      unity_prefs_put_string(RD_PREF_PLAYER_NAME, name);
      debugPrintf("[profile] player name from the Switch profile\n");
    } else {
      debugPrintf("[profile] no usable nickname: the game's player name stays\n");
    }
  }
  const char *stored = unity_prefs_get_string(RD_PREF_COUNTRY);
  if (config.country[0] && rd_player_country_unset(stored)) {
    unity_prefs_put_string(RD_PREF_COUNTRY, config.country);
    debugPrintf("[profile] country %s (was %s)\n", config.country, stored ? stored : "unset");
  }
}
