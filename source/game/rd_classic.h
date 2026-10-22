/* rd_classic.h -- optional classic drift scoring (config.txt classic_scoring): the
 * PointsManager rules of Real Drift 4.9 and earlier (same since 2.4) instead of 5.0.8's.
 *
 * 4.9 pays for every good corner: a corner's points are collected WITHOUT the multiplier and
 * banked as `partial x combo` on the transition (the first frame without drift points); each
 * corner over 1000 points raises the combo by one; driftTimeFactor grows 1.0/s and restarts at
 * every transition; 1 s without drifting ends the combo; a wall hit loses the current corner.
 * 5.0.8 instead multiplies every frame, grows the combo on doubling series points and keeps the
 * points on a hit (analysis/research/19-scoring-49-vs-508.md).
 *
 * The stock FixedUpdate still runs (angle, speed, proximity raycasts, slow motion, HUD
 * visibility); rd_classic_hook.c wraps it and replaces the scoring state with rd_classic_step.
 * Pure logic here (host-tested, tests/c/test_rd_classic.c). MIT (see LICENSE). */
#ifndef RD_CLASSIC_H
#define RD_CLASSIC_H

#include <stdint.h>

/* 4.9 PointsManager constants. */
#define RD_CLASSIC_ANGLE_MIN      5.0f    /* degrees; below: no drift points (a transition) */
#define RD_CLASSIC_ANGLE_MAX      140.0f  /* degrees; above: no drift points (5.0.8: no limit) */
#define RD_CLASSIC_COMBO_POINTS   1000    /* corner points that raise the combo */
#define RD_CLASSIC_COMBO_ONCE_MAX 2000    /* ...once per corner while below this */
#define RD_CLASSIC_COMBO_POW_MAX  16      /* powOf2 table size - 1 */
#define RD_CLASSIC_COMBO_TIMEOUT  1.0f    /* s without drifting that end the combo */

/* The scoring state, kept in the PointsManager's own fields between frames. */
typedef struct {
  int32_t partial;        /* points of the current corner, without the combo */
  int32_t total;          /* banked points (what the race result reads) */
  int32_t combo;          /* driftComboFactor, >= 1 */
  int32_t best_combo;
  float   time_factor;    /* driftTimeFactor */
  float   timer;          /* driftTimer: s since the last drift frame */
  int     combo_updated;  /* the corner already raised the combo by the 1000-point rule */
  int     drift_combo;    /* a combo is running (the HUD shows it) */
} rd_classic;

/* What one FixedUpdate saw. */
typedef struct {
  int   active;           /* right direction, on track, points enabled; else the combo is reset */
  int   race_ended;       /* banks what is left */
  float angle;            /* drift angle, degrees, >= 0 (0 = not drifting) */
  float speed_factor;     /* velocity m/s x 0.1 */
  int   proximity;        /* proximityFactor of the previous frame, >= 1 */
  float dt;               /* Time.deltaTime of the fixed step */
} rd_classic_in;

/* One FixedUpdate of 4.9's PointsManager scoring. Returns the points added to the corner this
 * frame (deltaPoints). */
int32_t rd_classic_step(rd_classic *s, const rd_classic_in *in);

/* Mathf.RoundToInt: nearest, halves to even. */
int32_t rd_classic_round(float f);

/* ACTk ObscuredInt as the game stores it: hiddenValue = value ^ currentCryptoKey. */
typedef struct { int32_t key, hidden; } rd_obscured_int;
/* The value. Only meaningful when key != 0: a field never assigned through the game has key
 * 0 and would need ACTk's static key (the hook leaves such frames alone). */
int32_t rd_obscured_get(rd_obscured_int o);
/* Stores v under the field's own key (RD_OBSCURED_KEY if it has none). */
void rd_obscured_set(rd_obscured_int *o, int32_t v);
#define RD_OBSCURED_KEY 444444            /* ACTk's default cryptoKey */

/* ---- glue (rd_classic_hook.c) -------------------------------------------------------- */
/* With classic_scoring on, wraps PointsManager.FixedUpdate once Assembly-CSharp is loaded.
 * 0 = not loaded yet (call again), 1 = done (hooked, off, or failed for good; logged). */
int rd_classic_try_install(void);

#endif
