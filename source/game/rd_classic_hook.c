/* rd_classic_hook.c -- classic (4.9) drift scoring, il2cpp glue (see rd_classic.h).
 * MIT (see LICENSE).
 *
 * Unity calls PointsManager.FixedUpdate through il2cpp_runtime_invoke, which reads
 * MethodInfo.methodPointer, so swapping that pointer wraps it (as rd_race_hook.c does with
 * UIManager.Update). The wrapper keeps the scoring state in the PointsManager's own fields:
 * before the stock method it saves them, the stock method runs (drift angle, speed,
 * proximity raycasts and arrows, slow motion, HUD visibility, its own 5.0.8 scoring), then
 * rd_classic_step continues from the saved state and its result overwrites the fields and the
 * score texts. A new race has a new PointsManager, so nothing is kept here.
 *
 * Invariant between frames: totalPoints = oldTotalPoints = the banked points. The stock
 * OnCollisionEnter banks `oldTotalPoints = totalPoints` and clears partialPoints and the
 * combo, which under this invariant is exactly 4.9's "Points Lost!" (the corner is lost), so
 * it is left alone; it also resets driftTimeFactor, which 4.9 did not. The stock angle test
 * (>= 10 degrees, 4.9: >= 5) is kept; 4.9's 140-degree limit is applied on top.
 * Main thread (FixedUpdate). */
#include <stdint.h>
#include <stdio.h>
#include "rd_classic.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "config.h"
#include "util.h"

static const char *const PM_FIELDS[] = RD_PM_FIELD_NAMES;
enum {                                   /* indexes into PM_FIELDS */
  PM_TOTAL, PM_OLD_TOTAL, PM_COMBO, PM_PARTIAL, PM_OLD_PARTIAL, PM_DELTA, PM_BEST_COMBO,
  PM_PROXIMITY, PM_TIME_FACTOR, PM_TIMER, PM_ANGLE, PM_SPEED_FACTOR, PM_TMP_PARTIAL,
  PM_DRIFT_COMBO, PM_COMBO_UPDATED, PM_RACE_ENDED, PM_COLLISION, PM_CALC_ENABLED,
  PM_CALC_POINTS, PM_CAR_DYNAMICS, PM_WRONG_DIRECTION, PM_TOTAL_TEXT, PM_PARTIAL_TEXT,
  PM_COMBO_TEXT, PM_SLIDER,
  PM_COUNT = sizeof PM_FIELDS / sizeof PM_FIELDS[0]
};
_Static_assert(PM_COUNT == 25, "RD_PM_FIELD_NAMES order must match the PM_* enum");
static const char *const WDC_FIELDS[] = RD_WDC_FIELD_NAMES;
enum { WDC_WRONG_DIRECTION, WDC_PLAYER_BEHIND, WDC_COUNT = sizeof WDC_FIELDS / sizeof WDC_FIELDS[0] };

typedef void  (*fn_void)(void *self, const void *mi);
typedef void  (*fn_set_obj)(void *self, void *value, const void *mi);
typedef void  (*fn_set_float)(void *self, float value, const void *mi);
typedef float (*fn_get_float)(void);

static fn_void g_orig_fixed_update;
static struct { fn_set_obj fn; const void *mi; } g_set_text;
static struct { fn_set_float fn; const void *mi; } g_set_fill;
static fn_get_float g_delta_time;
static uint32_t g_pm[PM_COUNT], g_wdc[WDC_COUNT], g_off_track;
static int g_done, g_unkeyed_logged;

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))

/* The scoring state as the fields hold it; 0 if an ObscuredInt has no key yet. */
static int load(const char *pm, rd_classic *s) {
  const rd_obscured_int total = FIELD(pm, g_pm[PM_TOTAL], rd_obscured_int);
  const rd_obscured_int combo = FIELD(pm, g_pm[PM_COMBO], rd_obscured_int);
  if (!total.key || !combo.key) return 0;
  s->total = rd_obscured_get(total);
  s->combo = rd_obscured_get(combo);
  s->partial = FIELD(pm, g_pm[PM_PARTIAL], int32_t);
  s->best_combo = FIELD(pm, g_pm[PM_BEST_COMBO], int32_t);
  s->time_factor = FIELD(pm, g_pm[PM_TIME_FACTOR], float);
  s->timer = FIELD(pm, g_pm[PM_TIMER], float);
  s->drift_combo = FIELD(pm, g_pm[PM_DRIFT_COMBO], uint8_t);
  s->combo_updated = FIELD(pm, g_pm[PM_COMBO_UPDATED], uint8_t);
  if (s->combo < 1) s->combo = 1;
  return 1;
}

/* Writes the state back; oldTotalPoints = totalPoints keeps the stock bank paths harmless. */
static void store(char *pm, const rd_classic *s, int32_t delta) {
  rd_obscured_set(&FIELD(pm, g_pm[PM_TOTAL], rd_obscured_int), s->total);
  rd_obscured_set(&FIELD(pm, g_pm[PM_OLD_TOTAL], rd_obscured_int), s->total);
  rd_obscured_set(&FIELD(pm, g_pm[PM_COMBO], rd_obscured_int), s->combo);
  FIELD(pm, g_pm[PM_OLD_PARTIAL], int32_t) = s->partial - delta;
  FIELD(pm, g_pm[PM_PARTIAL], int32_t) = s->partial;
  FIELD(pm, g_pm[PM_DELTA], int32_t) = delta;
  FIELD(pm, g_pm[PM_BEST_COMBO], int32_t) = s->best_combo;
  FIELD(pm, g_pm[PM_TIME_FACTOR], float) = s->time_factor;
  FIELD(pm, g_pm[PM_TIMER], float) = s->timer;
  FIELD(pm, g_pm[PM_DRIFT_COMBO], uint8_t) = (uint8_t)s->drift_combo;
  FIELD(pm, g_pm[PM_COMBO_UPDATED], uint8_t) = (uint8_t)s->combo_updated;
}

/* TMP_Text.text = s, if the label exists. */
static void set_text(void *label, const char *s) {
  if (label) g_set_text.fn(label, g_il2cpp.string_new(s), g_set_text.mi);
}

static void set_number(void *label, int32_t v) {
  char buf[16];
  snprintf(buf, sizeof buf, "%d", (int)v);
  set_text(label, buf);
}

/* The score HUD as 4.9 showed it: the banked total (also after the finish, when the stock
 * method would show it plus its own corner points), and during the race the combo, the corner
 * points without the combo and the radial slider as the time left before the combo ends. */
static void show(char *pm, const rd_classic *s, int race_ended) {
  if (FIELD(pm, g_pm[PM_CALC_POINTS], uint8_t)) set_number(FIELD(pm, g_pm[PM_TOTAL_TEXT], void *), s->total);
  if (race_ended) return;
  void *partial = FIELD(pm, g_pm[PM_PARTIAL_TEXT], void *);
  void *slider = FIELD(pm, g_pm[PM_SLIDER], void *);
  float fill = 0.0f;
  if (FIELD(pm, g_pm[PM_COLLISION], uint8_t) && FIELD(pm, g_pm[PM_TMP_PARTIAL], float) != 0.0f) {
    set_text(partial, RD_TXT_POINTS_LOST);
  } else {
    if (s->partial) set_number(partial, s->partial);
    else set_text(partial, "");
    if (s->drift_combo) fill = 1.0f - s->timer / RD_CLASSIC_COMBO_TIMEOUT;
  }
  set_number(FIELD(pm, g_pm[PM_COMBO_TEXT], void *), s->combo);
  if (slider) g_set_fill.fn(slider, fill, g_set_fill.mi);
}

/* Wrapper of PointsManager.FixedUpdate: the stock method, then 4.9 scoring over its inputs. */
static void fixed_update(void *self, const void *mi) {
  char *pm = self;
  char *car = pm ? FIELD(pm, g_pm[PM_CAR_DYNAMICS], char *) : NULL;
  char *wdc = pm ? FIELD(pm, g_pm[PM_WRONG_DIRECTION], char *) : NULL;
  rd_classic s;
  if (!car || !wdc || !load(pm, &s)) {    /* not started yet: stock only */
    if (car && wdc && !g_unkeyed_logged) {
      g_unkeyed_logged = 1;
      debugPrintf("[classic] " RD_CLS_POINTS_MANAGER " points have no crypto key; stock scoring\n");
    }
    g_orig_fixed_update(self, mi);
    return;
  }
  const int proximity = FIELD(pm, g_pm[PM_PROXIMITY], int32_t);
  g_orig_fixed_update(self, mi);

  const int race_ended = FIELD(pm, g_pm[PM_RACE_ENDED], uint8_t);
  const rd_classic_in in = {
    .active = !FIELD(wdc, g_wdc[WDC_WRONG_DIRECTION], uint8_t) &&
              !FIELD(wdc, g_wdc[WDC_PLAYER_BEHIND], uint8_t) &&
              !FIELD(car, g_off_track, uint8_t) && FIELD(pm, g_pm[PM_CALC_ENABLED], uint8_t),
    .race_ended = race_ended,
    .angle = FIELD(pm, g_pm[PM_ANGLE], float),
    .speed_factor = FIELD(pm, g_pm[PM_SPEED_FACTOR], float),
    .proximity = proximity > 1 ? proximity : 1,
    .dt = g_delta_time(),
  };
  const int32_t delta = rd_classic_step(&s, &in);
  store(pm, &s, delta);
  if (in.active) show(pm, &s, race_ended);
}

/* Field offsets of a game class by name; 1 if all were found. */
static int game_fields(const char *cls, const char *const names[], int n, uint32_t out[]) {
  void *klass = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, cls);
  return klass && rd_il2cpp_fields(&g_il2cpp, klass, names, n, out);
}

/* A method's MethodInfo by class and name, or NULL. */
static void **method(const char *assembly, const char *ns, const char *cls, const char *name,
                     int argc) {
  void *klass = rd_il2cpp_class(&g_il2cpp, assembly, ns, cls);
  void **m = klass ? g_il2cpp.class_get_method_from_name(klass, name, argc) : NULL;
  return m && m[0] ? m : NULL;
}

int rd_classic_try_install(void) {
  if (g_done || !config.classic_scoring) return 1;
  if (!g_il2cpp.class_get_method_from_name || !g_il2cpp.string_new || !g_il2cpp.resolve_icall)
    return 0;
  void **fu = method(RD_ASM_GAME, RD_NS_GAME, RD_CLS_POINTS_MANAGER, RD_MTH_FIXED_UPDATE, 0);
  if (!fu) return 0;                                           /* not loaded yet */
  g_done = 1;
  static const char *const OFF_TRACK[] = { RD_FLD_ALL_WHEELS_OFF };
  void **text = method(RD_ASM_TMP, RD_NS_TMP, RD_CLS_TMP_TEXT, RD_MTH_SET_TEXT, 1);
  void **fill = method(RD_ASM_UI, RD_NS_UI, RD_CLS_IMAGE, RD_MTH_SET_FILL_AMOUNT, 1);
  g_delta_time = (fn_get_float)g_il2cpp.resolve_icall(RD_ICALL_DELTA_TIME);
  if (!game_fields(RD_CLS_POINTS_MANAGER, PM_FIELDS, PM_COUNT, g_pm) ||
      !game_fields(RD_CLS_WRONG_DIRECTION, WDC_FIELDS, WDC_COUNT, g_wdc) ||
      !game_fields(RD_CLS_CAR_DYNAMICS, OFF_TRACK, 1, &g_off_track) ||
      !text || !fill || !g_delta_time) {
    debugPrintf("[classic] fields/methods not found -- classic scoring OFF\n");
    return 1;
  }
  g_set_text.fn = (fn_set_obj)text[0]; g_set_text.mi = text;
  g_set_fill.fn = (fn_set_float)fill[0]; g_set_fill.mi = fill;
  g_orig_fixed_update = (fn_void)fu[0];
  fu[0] = (void *)fixed_update;
  debugPrintf("[classic] " RD_CLS_POINTS_MANAGER "." RD_MTH_FIXED_UPDATE
              " hooked: classic (4.9) drift scoring\n");
  return 1;
}
