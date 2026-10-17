/* rd_race_hook.c -- race HUD il2cpp glue (see rd_race.h). MIT (see LICENSE).
 *
 * UIManager owns the on-screen controls; RaceManager the pause menu. Unity calls
 * UIManager.Update through il2cpp_runtime_invoke, which reads MethodInfo.methodPointer on
 * every call, so swapping that pointer wraps Update. Field offsets are looked up by name. */
#include <stdint.h>
#include "rd_race.h"
#include "rd_smoke.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "nx_pointer.h"
#include "config.h"
#include "util.h"

static const char *const UIM_FIELDS[] = RD_UIM_FIELD_NAMES;
enum {                                   /* indexes into UIM_FIELDS */
  UIM_RACE_MANAGER,
  UIM_FIRST_TOUCH_CONTROL,               /* [1..] the touch driving controls (GameObjects) */
  UIM_COUNT = sizeof UIM_FIELDS / sizeof UIM_FIELDS[0]
};
static const char *const RM_FIELDS[] = RD_RM_FIELD_NAMES;
enum { RM_PAUSE_PANEL, RM_IS_RACE_ENDED, RM_IS_PHOTO_MODE,
       RM_COUNT = sizeof RM_FIELDS / sizeof RM_FIELDS[0] };
_Static_assert(RM_COUNT == 3, "RD_RM_FIELD_NAMES order must match the RM_* enum");

static uint32_t g_uim[UIM_COUNT], g_rm[RM_COUNT];

typedef void    (*fn_void)(void *self, const void *mi);
typedef uint8_t (*fn_get_bool)(void *self, const void *mi);
typedef void    (*fn_set_bool)(void *self, uint8_t value, const void *mi);

static fn_void g_orig_update;
static struct { fn_get_bool fn; const void *mi; } g_active_self;
static struct { fn_set_bool fn; const void *mi; } g_set_active;
static struct { fn_void fn; const void *mi; } g_pause_on, g_pause_off;
static volatile uint32_t g_last_race_frame = 0;
static uint64_t g_pause_held_prev = 0;
static int g_installed = 0;

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))

int rd_race_ui_installed(void) { return g_installed; }

int rd_race_active(void) {
  return g_installed && (port_frame_count() - g_last_race_frame) < RD_ACTIVE_WINDOW_FRAMES;
}

/* Deactivates every on-screen driving control GameObject that is active (hide_touch_controls). */
static void hide_touch_controls(void *uim) {
  for (int i = UIM_FIRST_TOUCH_CONTROL; i < UIM_COUNT; i++) {
    void *go = FIELD(uim, g_uim[i], void *);
    if (go && g_active_self.fn(go, g_active_self.mi)) g_set_active.fn(go, 0, g_set_active.mi);
  }
}

/* Wrapper of UIManager.Update (runs every race frame): marks the race active, hides the touch
 * controls, lets the smoke hook rescan, and handles the pause button. */
static void ui_update(void *self, const void *mi) {
  g_orig_update(self, mi);
  if (!self) return;
  g_last_race_frame = port_frame_count();

  if (config.hide_touch_controls) hide_touch_controls(self);
  rd_smoke_race_frame();

  uint64_t held = nxp_pad_held() & config.pause;
  int pressed = held && !g_pause_held_prev;
  g_pause_held_prev = held;
  char *rm = FIELD(self, g_uim[UIM_RACE_MANAGER], char *);
  if (!pressed || !rm) return;
  void *panel = FIELD(rm, g_rm[RM_PAUSE_PANEL], void *);
  int panel_on = panel ? g_active_self.fn(panel, g_active_self.mi) : 0;
  switch (rd_pause_action(pressed, panel_on, FIELD(rm, g_rm[RM_IS_PHOTO_MODE], uint8_t),
                          FIELD(rm, g_rm[RM_IS_RACE_ENDED], uint8_t))) {
    case RD_PAUSE_ENABLE:  if (g_pause_on.fn)  g_pause_on.fn(rm, g_pause_on.mi);   break;
    case RD_PAUSE_DISABLE: if (g_pause_off.fn) g_pause_off.fn(rm, g_pause_off.mi); break;
    default: break;
  }
}

int rd_race_ui_install(void *img_game, void *img_core) {
  const rd_il2cpp_api *api = &g_il2cpp;
  if (g_installed) return 1;
  if (!api->class_from_name || !api->class_get_method_from_name) return -1;
  void *ui = api->class_from_name(img_game, RD_NS_GAME, RD_CLS_UI_MANAGER);
  void *rm = api->class_from_name(img_game, RD_NS_GAME, RD_CLS_RACE_MANAGER);
  void *go = img_core ? api->class_from_name(img_core, RD_NS_UNITY, RD_CLS_GAME_OBJECT) : NULL;
  if (!ui || !rm || !go) return 0;
  void **upd = api->class_get_method_from_name(ui, RD_MTH_UPDATE, 0);
  void **as  = api->class_get_method_from_name(go, RD_MTH_GET_ACTIVE_SELF, 0);
  void **sa  = api->class_get_method_from_name(go, RD_MTH_SET_ACTIVE, 1);
  void **pon = api->class_get_method_from_name(rm, RD_MTH_PAUSE_ON, 0);
  void **pof = api->class_get_method_from_name(rm, RD_MTH_PAUSE_OFF, 0);
  if (!rd_il2cpp_fields(api, ui, UIM_FIELDS, UIM_COUNT, g_uim) ||
      !rd_il2cpp_fields(api, rm, RM_FIELDS, RM_COUNT, g_rm)) {
    debugPrintf("[race] " RD_CLS_UI_MANAGER "/" RD_CLS_RACE_MANAGER
                " fields not found -- race UI hook OFF\n");
    return -1;
  }
  if (!upd || !upd[0] || !as || !sa) {
    debugPrintf("[race] " RD_CLS_UI_MANAGER "." RD_MTH_UPDATE " / " RD_CLS_GAME_OBJECT
                " methods not found -- race UI hook OFF\n");
    return -1;
  }
  g_active_self.fn = (fn_get_bool)as[0]; g_active_self.mi = as;
  g_set_active.fn  = (fn_set_bool)sa[0]; g_set_active.mi = sa;
  if (pon) { g_pause_on.fn  = (fn_void)pon[0]; g_pause_on.mi  = pon; }
  if (pof) { g_pause_off.fn = (fn_void)pof[0]; g_pause_off.mi = pof; }
  g_orig_update = (fn_void)upd[0];
  upd[0] = (void *)ui_update;
  g_installed = 1;
  debugPrintf("[race] " RD_CLS_UI_MANAGER "." RD_MTH_UPDATE
              " hooked: hide_touch_controls=%d, pause %s\n",
              config.hide_touch_controls, (pon && pof) ? "ok" : "MISSING");
  return 1;
}
