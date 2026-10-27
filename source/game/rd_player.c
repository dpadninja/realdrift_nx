/* rd_player.c -- see rd_player.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_player.h"
#include "rd_game_symbols.h"

int rd_player_country_unset(const char *stored) {
  return !stored || !stored[0] || !strcmp(stored, RD_COUNTRY_FAILED_LOOKUP);
}
