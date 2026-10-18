/* rd_drive.c -- see rd_drive.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_drive.h"

/* Clamps a stick value to -1..1, zeroes it inside the deadzone and rescales the rest so full
 * deflection is still 1. */
static float apply_deadzone(float x, float dz) {
  if (x > 1.0f) x = 1.0f;
  if (x < -1.0f) x = -1.0f;
  if (dz <= 0.0f) return x;
  if (dz >= 1.0f) return 0.0f;
  float a = x < 0 ? -x : x;
  if (a <= dz) return 0.0f;
  float v = (a - dz) / (1.0f - dz);    /* rescale so full deflection is still 1.0 */
  return x < 0 ? -v : v;
}

void rd_drive_step(const rd_bindings *b, rd_drive_state *st, uint64_t held, float stick_x,
                   rd_drive_out *out) {
  memset(out, 0, sizeof *out);
  for (int a = 0; a < RD_ACT_COUNT; a++) {
    int now = (held & b->action[a]) != 0;
    int was = (st->held_prev & b->action[a]) != 0;
    out->press[a]   = now && !was;
    out->release[a] = !now && was;
  }

  float steer = apply_deadzone(stick_x, b->deadzone);
  if (steer == 0.0f) {                       /* D-pad only when the stick is centred */
    if (held & b->steer_left)  steer -= 1.0f;
    if (held & b->steer_right) steer += 1.0f;
  }
  /* Write while steering, plus once on return to centre; otherwise leave the field to the
   * touch buttons so both input methods keep working. */
  out->write_steer = steer != 0.0f || st->steer_prev != 0.0f;
  out->steer = steer;
  out->clutch = (held & b->clutch) != 0;

  st->held_prev = held;
  st->steer_prev = steer;
}
