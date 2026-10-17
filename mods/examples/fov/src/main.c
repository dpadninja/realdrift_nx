/* fov -- scales every camera field of view the game sets. Shows inline_hook on a C# method:
 * UnityEngine.Camera.set_fieldOfView is called directly by the game's camera code, so
 * method_swap would not see those calls. MIT (see LICENSE). */
#include "rd_mod.h"

float strtof(const char *s, char **end);   /* resolved from the port's libc */

typedef void (*set_fov_fn)(void *camera, float fov, void *method);
static const rd_mod_api *api;
static set_fov_fn orig_set_fov;
static float scale = 1.0f;

/* Camera.set_fieldOfView(float): same C signature plus the trailing MethodInfo*. */
static void set_fov(void *camera, float fov, void *method) {
  orig_set_fov(camera, fov * scale, method);
}

static void on_ready(void *user) {
  (void)user;
  void *camera = api->find_class("UnityEngine.CoreModule", "UnityEngine", "Camera");
  void *code = api->method_code(api->method(camera, "set_fieldOfView", 1));
  int r = api->inline_hook(code, (void *)set_fov, (void **)&orig_set_fov);
  api->log(api, r == 0 ? "camera FOV x%d%%" : "hook failed (%d)", r == 0 ? (int)(scale * 100) : r);
}

RD_MOD_ENTRY {
  api = m;
  *user = 0;
  const char *s = m->ini(m, "scale");
  if (s) scale = strtof(s, 0);
  if (scale < 0.5f || scale > 2.0f) scale = 1.0f;
  return m->on_game_ready(m, on_ready);
}
