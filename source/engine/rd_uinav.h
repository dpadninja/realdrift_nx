/* rd_uinav.h -- focus navigation over on-screen targets with a D-pad / stick: which target a
 * direction leads to, held-direction auto-repeat, and keeping the focus when the target list
 * changes. Knows neither Unity nor the game: targets are an id and a rectangle (screen pixels,
 * top-left origin, y down). The uGUI source is rd_ugui.h, the game's use rd_menu.h.
 * Pure logic, host-tested (tests/c/test_rd_uinav.c). MIT (see LICENSE). */
#ifndef RD_UINAV_H
#define RD_UINAV_H

#include <stdint.h>

typedef struct { float x, y, w, h; } rd_uinav_rect;
typedef struct { uintptr_t id; rd_uinav_rect r; } rd_uinav_target;

enum { RD_UINAV_NONE = -1, RD_UINAV_LEFT, RD_UINAV_RIGHT, RD_UINAV_UP, RD_UINAV_DOWN };

/* Weight of the gap along a move against the offset across it (Android's FocusFinder: 13). */
#define RD_UINAV_MAJOR_WEIGHT  13.0f
#define RD_UINAV_REPEAT_DELAY 20      /* frames a direction is held before it repeats */
#define RD_UINAV_REPEAT_EVERY 6       /* frames between repeats */

/* The target a move in `dir` from t[from] leads to, or -1 (nothing in that direction): spatial
 * navigation as on a TV remote, following Android's FocusFinder (AOSP
 * frameworks/base/core/java/android/view/FocusFinder.java, Apache-2.0; the rules, not its
 * code). A candidate lies in the direction (its near edge past the focused one's, its far
 * edge further); one in the beam -- overlapping the focused element across the move, the
 * same row or column -- always beats one outside it (Android does that for left/right only;
 * menus of columns behave better with it both ways); between equals the smaller
 * MAJOR_WEIGHT x gap^2 + centre offset^2 wins, ties go to the first. If nothing qualifies, any
 * target whose centre is ahead may be picked, so the focus never gets stuck. */
int rd_uinav_step(const rd_uinav_target *t, int n, int from, int dir);

/* Where the focus is after the target list changed: the index of `id` if it is still there,
 * else the target nearest to `last` (the focused rectangle), else (has_last = 0) the top-left
 * one -- topmost row first, then leftmost. -1 if n == 0. */
int rd_uinav_refocus(const rd_uinav_target *t, int n, uintptr_t id, const rd_uinav_rect *last,
                     int has_last);

/* Auto-repeat of a held direction: 1 if the frame-th frame of holding (1 = just pressed)
 * should move. */
int rd_uinav_repeat(uint32_t frame);

/* Centre of a rectangle. */
void rd_uinav_center(const rd_uinav_rect *r, float *x, float *y);

/* Points to try when tapping a target: 0 = the centre, 1..4 = the centres of its quarters
 * (a label or icon may cover the middle). */
#define RD_UINAV_PROBES 5
void rd_uinav_probe(const rd_uinav_rect *r, int i, float *x, float *y);

/* Share of the screen the rectangle covers (0..; not clipped). */
float rd_uinav_screen_share(const rd_uinav_rect *r, float screen_w, float screen_h);

/* 1 if the rectangle has an area and overlaps the screen. */
int rd_uinav_on_screen(const rd_uinav_rect *r, float screen_w, float screen_h);

#endif
