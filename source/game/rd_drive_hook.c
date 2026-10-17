/* rd_drive_hook.c -- Switch controller driving for Real Drift (il2cpp glue). MIT (see LICENSE).
 *
 * Swaps MobileCarController.GetInput in the class vtable (and its MethodInfo) for a wrapper
 * that turns the controller state into the same calls the on-screen buttons make, then runs
 * the original GetInput. Pure logic: rd_drive.c. Names: rd_game_symbols.h. */
#include <stdint.h>
#include <switch.h>
#include "rd_drive.h"
#include "rd_race.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "rd_settings.h"
#include "nx_pointer.h"
#include "config.h"
#include "util.h"

#define CAR RD_CLS_CAR_CONTROLLER   /* for log strings */

typedef void (*fn_get_input)(void *self, float *throttle, float *brake, float *steer,
                             float *handbrake, float *clutch, uint8_t *start_engine,
                             uint8_t *change_camera, int32_t *target_gear, const void *mi);
typedef void (*fn_button)(void *self, const void *mi);

static uint32_t g_steer_off;                     /* MobileCarController.mobileSteerInput */
static fn_get_input g_orig_get_input;
static struct { fn_button fn; const void *mi; } g_press[RD_ACT_COUNT], g_release[RD_ACT_COUNT];
static int g_installed = 0, g_failed = 0;
static volatile uint32_t g_last_drive_frame = 0;
static rd_drive_state g_state;

/* Bindings come from config.txt (config.binds, rd_settings.h); RD_BTN_* must be libnx bits. */
_Static_assert(RD_BTN_A == HidNpadButton_A && RD_BTN_B == HidNpadButton_B &&
               RD_BTN_X == HidNpadButton_X && RD_BTN_Y == HidNpadButton_Y &&
               RD_BTN_LSTICK == HidNpadButton_StickL && RD_BTN_RSTICK == HidNpadButton_StickR &&
               RD_BTN_L == HidNpadButton_L && RD_BTN_R == HidNpadButton_R &&
               RD_BTN_ZL == HidNpadButton_ZL && RD_BTN_ZR == HidNpadButton_ZR &&
               RD_BTN_PLUS == HidNpadButton_Plus && RD_BTN_MINUS == HidNpadButton_Minus &&
               RD_BTN_LEFT == HidNpadButton_Left && RD_BTN_UP == HidNpadButton_Up &&
               RD_BTN_RIGHT == HidNpadButton_Right && RD_BTN_DOWN == HidNpadButton_Down,
               "rd_settings.h button bits must match libnx HidNpadButton");

static const char *const PRESS_NAMES[]   = RD_CAR_PRESS_METHODS;
static const char *const RELEASE_NAMES[] = RD_CAR_RELEASE_METHODS;
_Static_assert(sizeof PRESS_NAMES / sizeof PRESS_NAMES[0] == RD_ACT_COUNT &&
               sizeof RELEASE_NAMES / sizeof RELEASE_NAMES[0] == RD_ACT_COUNT,
               "RD_CAR_*_METHODS must list one method per rd_action");

int rd_drive_active(void) {
  return g_installed && (port_frame_count() - g_last_drive_frame) < RD_ACTIVE_WINDOW_FRAMES;
}

static void get_input(void *self, float *throttle, float *brake, float *steer,
                      float *handbrake, float *clutch, uint8_t *start_engine,
                      uint8_t *change_camera, int32_t *target_gear, const void *mi) {
  g_last_drive_frame = port_frame_count();
  if (self) {
    rd_drive_out o;
    rd_drive_step(&config.binds, &g_state, nxp_pad_held(), nxp_pad_stick_x(), &o);
    for (int a = 0; a < RD_ACT_COUNT; a++) {
      if (o.press[a] && g_press[a].fn)     g_press[a].fn(self, g_press[a].mi);
      if (o.release[a] && g_release[a].fn) g_release[a].fn(self, g_release[a].mi);
    }
    if (o.write_steer) *(float *)((char *)self + g_steer_off) = o.steer;
  }
  g_orig_get_input(self, throttle, brake, steer, handbrake, clutch, start_engine,
                   change_camera, target_gear, mi);
}

/* Gives up on controller driving for good (logs why); returns 0 for the caller to return. */
static int fail(const char *what) {
  g_failed = 1;
  debugPrintf("[drive] %s -- controller driving OFF\n", what);
  return 0;
}

static void bind_button(const rd_il2cpp_api *api, void *klass, const char *name, int a,
                        int release) {
  void **m = name ? api->class_get_method_from_name(klass, name, 0) : NULL;
  if (!m) { if (name) debugPrintf("[drive] warning: " CAR ".%s not found\n", name); return; }
  if (release) { g_release[a].fn = (fn_button)m[0]; g_release[a].mi = m; }
  else         { g_press[a].fn   = (fn_button)m[0]; g_press[a].mi   = m; }
}

/* Called every RD_DRIVE_RETRY_FRAMES until it succeeds (Assembly-CSharp must be loaded). */
int rd_drive_try_install(void) {
  const rd_il2cpp_api *api = &g_il2cpp;
  if (g_installed || g_failed) return g_installed;
  if (!api->domain_get || !api->class_get_method_from_name) return fail("il2cpp API missing");
  void *klass = rd_il2cpp_class(api, RD_ASM_GAME, RD_NS_GAME, CAR);
  if (!klass) return 0;                                   /* not loaded yet: retry later */
  if (!rd_race_ui_installed()) {
    void *dom = api->domain_get();
    void *game = api->domain_assembly_open(dom, RD_ASM_GAME);
    void *core = api->domain_assembly_open(dom, RD_ASM_UNITY_CORE);
    rd_race_ui_install(api->assembly_get_image(game), core ? api->assembly_get_image(core) : NULL);
  }

  if (!rd_il2cpp_field_offset(api, klass, RD_FLD_STEER_INPUT, &g_steer_off))
    return fail(CAR "." RD_FLD_STEER_INPUT " not found");
  void **get_input_mi = api->class_get_method_from_name(klass, RD_MTH_GET_INPUT,
                                                         RD_MTH_GET_INPUT_ARGC);
  if (!get_input_mi || !get_input_mi[0]) return fail(CAR "." RD_MTH_GET_INPUT " not found");
  for (int a = 0; a < RD_ACT_COUNT; a++) {
    bind_button(api, klass, PRESS_NAMES[a], a, 0);
    bind_button(api, klass, RELEASE_NAMES[a], a, 1);
  }

  uint64_t orig = (uint64_t)(uintptr_t)get_input_mi[0];  /* MethodInfo.methodPointer */
  int slot = rd_find_vtable_slot((const uint64_t *)klass, RD_IL2CPP_CLASS_SCAN_WORDS, orig,
                                 (uint64_t)(uintptr_t)get_input_mi);
  if (slot < 0) return fail(CAR "." RD_MTH_GET_INPUT " vtable slot not found");
  g_orig_get_input = (fn_get_input)(uintptr_t)orig;
  ((uint64_t *)klass)[slot] = (uint64_t)(uintptr_t)get_input;
  get_input_mi[0] = (void *)get_input;
  g_installed = 1;
  debugPrintf("[drive] " CAR "." RD_MTH_GET_INPUT " hooked (vtable word %d, orig %p); "
              "bindings from config.txt\n", slot, (void *)(uintptr_t)orig);
  return 1;
}
