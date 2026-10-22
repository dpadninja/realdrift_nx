/* rd_classic.c -- see rd_classic.h. Follows PointsManager.FixedUpdate of Real Drift 4.9
 * (analysis/src49/PointsManager.cs). MIT (see LICENSE). */
#include <math.h>
#include "rd_classic.h"

int32_t rd_classic_round(float f) {
  return (int32_t)nearbyintf(f);          /* default rounding mode: nearest, ties to even */
}

/* 1000 * 2^min(combo - 1, 16): the corner points that raise the combo again. */
static int32_t combo_threshold(int32_t combo) {
  int p = combo - 1;
  if (p < 0) p = 0;
  if (p > RD_CLASSIC_COMBO_POW_MAX) p = RD_CLASSIC_COMBO_POW_MAX;
  return RD_CLASSIC_COMBO_POINTS * ((int32_t)1 << p);
}

/* total + partial * combo, saturated at INT32_MAX (40 corners at x40 is ~2M; this is a guard). */
static int32_t bank(int32_t total, int32_t partial, int32_t combo) {
  const int64_t t = (int64_t)total + (int64_t)partial * combo;
  return t > INT32_MAX ? INT32_MAX : t < INT32_MIN ? INT32_MIN : (int32_t)t;
}

int32_t rd_classic_step(rd_classic *s, const rd_classic_in *in) {
  if (!in->active) {                      /* wrong way / off track: the corner and combo go */
    s->partial = 0;
    s->combo = 1;
    s->time_factor = 1.0f;
    return 0;
  }
  float angle = in->angle;
  if (angle < RD_CLASSIC_ANGLE_MIN || angle > RD_CLASSIC_ANGLE_MAX) angle = 0.0f;
  const float angle_factor = angle * 0.1f;
  /* Below 10 m/s the time factor does not count (4.9). */
  const float total_factor = in->speed_factor < 1.0f
      ? angle_factor * in->speed_factor * (float)in->proximity
      : angle_factor * in->speed_factor * s->time_factor * (float)in->proximity;
  const int32_t delta = rd_classic_round(total_factor);
  s->partial += delta;

  if (total_factor != 0.0f && !in->race_ended) {
    s->drift_combo = 1;
    s->timer = 0.0f;
    s->time_factor += in->dt;
    if (s->partial > 0) {
      if (s->partial > RD_CLASSIC_COMBO_POINTS && s->partial < RD_CLASSIC_COMBO_ONCE_MAX &&
          !s->combo_updated) {
        s->combo++;
        s->combo_updated = 1;
      } else if (s->partial > combo_threshold(s->combo)) {
        s->combo++;
      }
      if (s->best_combo < s->combo) s->best_combo = s->combo;
    }
  } else {                                /* transition: bank the corner with the combo */
    s->combo_updated = 0;
    s->time_factor = 1.0f;
    s->total = bank(s->total, s->partial, s->combo);
    s->partial = 0;
    s->timer += in->dt;
    if (s->timer > RD_CLASSIC_COMBO_TIMEOUT) {
      s->timer = 0.0f;
      s->combo = 1;
      s->drift_combo = 0;
    }
  }
  return delta;
}

int32_t rd_obscured_get(rd_obscured_int o) {
  return o.hidden ^ o.key;
}

void rd_obscured_set(rd_obscured_int *o, int32_t v) {
  if (!o->key) o->key = RD_OBSCURED_KEY;
  o->hidden = v ^ o->key;
}
