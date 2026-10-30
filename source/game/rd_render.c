/* rd_render.c -- see rd_render.h. MIT (see LICENSE). */
#include "rd_render.h"

int rd_render_aa_valid(int samples) {
  return samples == 0 || samples == 2 || samples == 4 || samples == 8;
}

float rd_render_far(float cur, int wanted) {
  return wanted > RD_DRAW_DISTANCE_GAME && (float)wanted > cur ? (float)wanted : cur;
}
