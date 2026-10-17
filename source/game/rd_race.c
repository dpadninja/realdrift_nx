/* rd_race.c -- see rd_race.h. MIT (see LICENSE). */
#include "rd_race.h"

int rd_pause_action(int pressed, int pause_panel_active, int photo_mode, int race_ended) {
  if (!pressed || photo_mode || race_ended) return RD_PAUSE_NONE;
  return pause_panel_active ? RD_PAUSE_DISABLE : RD_PAUSE_ENABLE;
}
