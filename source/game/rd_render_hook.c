/* rd_render_hook.c -- draw_distance and antialiasing, il2cpp glue (see rd_render.h).
 * MIT (see LICENSE).
 *
 * Runs from the main loop between engine frames, so the icalls never interleave with the
 * game's C#. Every RD_RENDER_FRAMES frames: Camera.main (a fresh reference, never kept, so it
 * is alive), its far plane raised if shorter, and the quality level's antiAliasing set. The
 * main camera of a race is a scene object: each track load brings a new one. Main thread. */
#include <stdint.h>
#include "rd_render.h"
#include "rd_il2cpp.h"
#include "rd_quality.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "config.h"
#include "util.h"

static int g_state;                              /* 0 = not tried, 1 = ready, -1 = off */
static struct {
  void *(*camera_main)(void);
  float (*get_far)(void *camera);
  void (*set_far)(void *camera, float value);
} ic;
static void *g_logged;                           /* the last camera logged (compared only) */

/* Resolves the icalls once the game is loaded. 1 = ready, 0 = not yet, -1 = off. */
static int install(void) {
  if (!rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_PERSISTENT_DATA)) return 0;
  ic.camera_main = g_il2cpp.resolve_icall(RD_ICALL_CAMERA_MAIN);
  ic.get_far     = g_il2cpp.resolve_icall(RD_ICALL_GET_FAR_CLIP);
  ic.set_far     = g_il2cpp.resolve_icall(RD_ICALL_SET_FAR_CLIP);
  if (!ic.camera_main || !ic.get_far || !ic.set_far) {
    debugPrintf("[render] Camera icalls not found: draw_distance off\n");
    ic.camera_main = NULL;
  }
  return 1;
}

/* The main camera's far plane raised to draw_distance. */
static void apply_far(void) {
  void *cam = ic.camera_main ? ic.camera_main() : NULL;
  if (!cam) return;
  const float cur = ic.get_far(cam), far = rd_render_far(cur, config.draw_distance);
  if (far == cur) return;
  ic.set_far(cam, far);
  if (cam != g_logged) {
    g_logged = cam;
    debugPrintf("[render] main camera far plane %.0f -> %.0f m\n", (double)cur, (double)far);
  }
}

void rd_render_frame(void) {
  if (g_state < 0 || (port_frame_count() % RD_RENDER_FRAMES)) return;
  if (config.draw_distance == RD_DRAW_DISTANCE_GAME && !config.antialiasing) {
    g_state = -1;
    return;
  }
  if (!g_state && (g_state = install()) <= 0) return;
  if (config.antialiasing) rd_quality_set_aa(config.antialiasing);
  if (config.draw_distance != RD_DRAW_DISTANCE_GAME) apply_far();
}
