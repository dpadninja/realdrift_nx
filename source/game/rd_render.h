/* rd_render.h -- config.txt min_draw_distance and antialiasing (experimental).
 *
 * Draw distance: the game has no LODs, terrain, occlusion culling or fog; what is drawn is cut
 * only by the far clip plane of the scene's Main Camera, which the scenes set to 360 m on the
 * two mountain tracks (1.4 km across) and 1000-4000 m elsewhere (analysis/research/34). In
 * races min_draw_distance is a floor for the main camera's far plane (a farther one is kept);
 * the menu and garage cameras (50 m) are left alone.
 * Antialiasing: the current quality level's antiAliasing (rd_quality.h) for the cameras'
 * render targets, and a multisampled window (egl.c) for the frame drawn straight to it.
 *
 * Pure part host-testable (tests/c/test_rd_render.c); the il2cpp glue is rd_render_hook.c,
 * called from the main loop between engine frames. MIT (see LICENSE). */
#ifndef RD_RENDER_H
#define RD_RENDER_H

#include <stdint.h>

#define RD_DRAW_DISTANCE_GAME  0      /* config value: the scene's own far plane */
#define RD_DRAW_DISTANCE_MAX   5000   /* metres; the largest track is 1.4 km across */
#define RD_RENDER_FRAMES       30     /* frames between checks of the main camera */

/* 1 if `samples` is an antialiasing value the engine accepts: 0 (off), 2, 4 or 8. */
int rd_render_aa_valid(int samples);

/* The far plane to use for a camera whose far plane is `cur` metres: `wanted` if that is
 * farther, else `cur` (RD_DRAW_DISTANCE_GAME keeps `cur`). */
float rd_render_far(float cur, int wanted);

/* 1 if the far plane is applied now: a race is active (`race_active`, rd_race_active) and the
 * active scene changed at frame `scene_frame` more than RD_ACTIVE_WINDOW_FRAMES before `now`,
 * so the race activity was seen in this scene and is not left over from the last one. */
int rd_render_far_due(int race_active, uint32_t now, uint32_t scene_frame);

/* Main loop, after each engine frame: applies antialiasing once il2cpp is up and, in races,
 * the far plane to the main camera. Does nothing when both settings are off. Main thread. */
void rd_render_frame(void);

#endif
