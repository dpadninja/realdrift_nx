/* rd_render_hook.c -- min_draw_distance and antialiasing, il2cpp glue (see rd_render.h).
 * MIT (see LICENSE).
 *
 * Runs from the main loop between engine frames, so the icalls never interleave with the
 * game's C#. Every RD_RENDER_FRAMES frames: the quality level's antiAliasing set, and during a
 * race (rd_race_active) Camera.main (a fresh reference, never kept, so it is alive) gets its far
 * plane raised if shorter. Only in races: the menu and garage cameras stop at 50 m on purpose.
 * rd_race_active alone is not enough: the garage loads in one long frame right after the race,
 * so the race's last frames still count as recent there (rd_render_far_due). The main camera
 * of a race is a scene object: each track load brings a new one. Main thread. */
#include <stdint.h>
#include "rd_render.h"
#include "rd_il2cpp.h"
#include "rd_quality.h"
#include "rd_race.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "config.h"
#include "util.h"

static int g_state;                              /* 0 = not tried, 1 = ready, -1 = off */
static struct {
  void *(*camera_main)(void);
  float (*get_far)(void *camera);
  void (*set_far)(void *camera, float value);
  void (*active_scene)(int32_t *handle);         /* GetActiveScene_Injected(out Scene) */
} ic;
static void *g_logged;                           /* the last camera logged (compared only) */
static int32_t g_scene;                          /* the active scene's handle */
static uint32_t g_scene_frame;                   /* when a change of it was seen */

/* Resolves the icalls once the game is loaded. 1 = ready, 0 = not yet, -1 = off. */
static int install(void) {
  if (!rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_PERSISTENT_DATA)) return 0;
  ic.camera_main = g_il2cpp.resolve_icall(RD_ICALL_CAMERA_MAIN);
  ic.get_far     = g_il2cpp.resolve_icall(RD_ICALL_GET_FAR_CLIP);
  ic.set_far     = g_il2cpp.resolve_icall(RD_ICALL_SET_FAR_CLIP);
  ic.active_scene = g_il2cpp.resolve_icall(RD_ICALL_ACTIVE_SCENE);
  if (!ic.camera_main || !ic.get_far || !ic.set_far || !ic.active_scene) {
    debugPrintf("[render] Camera icalls not found: min_draw_distance off\n");
    ic.camera_main = NULL;
  }
  return 1;
}

/* The race camera's far plane raised to min_draw_distance. */
static void apply_far(void) {
  void *cam = ic.camera_main ? ic.camera_main() : NULL;
  if (!cam) return;
  const float cur = ic.get_far(cam), far = rd_render_far(cur, config.min_draw_distance);
  if (far == cur) return;
  ic.set_far(cam, far);
  if (cam != g_logged) {
    g_logged = cam;
    debugPrintf("[render] main camera far plane %.0f -> %.0f m\n", (double)cur, (double)far);
  }
}

void rd_render_frame(void) {
  if (g_state < 0 || (port_frame_count() % RD_RENDER_FRAMES)) return;
  if (config.min_draw_distance == RD_DRAW_DISTANCE_GAME && !config.antialiasing) {
    g_state = -1;
    return;
  }
  if (!g_state && (g_state = install()) <= 0) return;
  if (config.antialiasing) rd_quality_set_aa(config.antialiasing);
  if (config.min_draw_distance == RD_DRAW_DISTANCE_GAME || !ic.camera_main) return;
  const uint32_t now = port_frame_count();
  int32_t scene = 0;
  ic.active_scene(&scene);
  if (scene != g_scene) {
    g_scene = scene;
    g_scene_frame = now;
  }
  if (rd_render_far_due(rd_race_active(), now, g_scene_frame)) apply_far();
}
