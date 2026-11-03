/* rd_slider_grab.c -- see rd_slider_grab.h. il2cpp glue. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_slider_grab.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_ugui.h"
#include "util.h"

typedef void (*fn_handler)(void *self, const void *method);

static int g_state;                       /* 0 not tried, 1 ready, -1 not found */
static void *g_rm_type;                   /* System.Type of ReplayManager */
static uint32_t g_slider_off;             /* ReplayManager.replaySlider */
static const void *g_press, *g_release;   /* their MethodInfo (first word: the code) */
static void *(*g_find_objects)(void *type);
static void *g_held_rm;                   /* the ReplayManager whose slider is held */

/* Resolves ReplayManager, its slider field and the two handlers once. */
static int ready(void) {
  if (g_state) return g_state > 0;
  g_state = -1;
  void *rm = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_REPLAY_MANAGER);
  g_find_objects = g_il2cpp.resolve_icall(RD_ICALL_FIND_OBJECTS);
  void **press = rm ? g_il2cpp.class_get_method_from_name(rm, RD_MTH_REPLAY_PRESS, 0) : NULL;
  void **release = rm ? g_il2cpp.class_get_method_from_name(rm, RD_MTH_REPLAY_RELEASE, 0) : NULL;
  if (!rm || !g_find_objects || !g_il2cpp.class_get_type || !press || !press[0] || !release ||
      !release[0] || !rd_il2cpp_field_offset(&g_il2cpp, rm, RD_FLD_REPLAY_SLIDER, &g_slider_off)) {
    debugPrintf("[uinav] " RD_CLS_REPLAY_MANAGER " not found: the replay slider follows touch only\n");
    return 0;
  }
  g_rm_type = g_il2cpp.type_get_object(g_il2cpp.class_get_type(rm));
  g_press = press;
  g_release = release;
  g_state = g_rm_type ? 1 : -1;
  return g_state > 0;
}

/* The scene's ReplayManager whose slider is `slider`, or NULL. */
static void *replay_of(void *slider) {
  void *arr = g_find_objects(g_rm_type);
  const uint32_t n = arr ? (uint32_t)g_il2cpp.array_length(arr) : 0;
  for (uint32_t i = 0; i < n; i++) {
    void *rm = rd_il2cpp_array_at(arr, i);
    if (rm && *(void **)((char *)rm + g_slider_off) == slider) return rm;
  }
  return NULL;
}

/* Calls a parameterless ReplayManager handler on rm. */
static void call(const void *method, void *rm) {
  ((fn_handler)(*(void *const *)method))(rm, method);
}

void rd_slider_grab(void *slider, int down) {
  if (!down) {
    if (g_held_rm && rd_ugui_alive(g_held_rm)) call(g_release, g_held_rm);
    g_held_rm = NULL;
    return;
  }
  if (g_held_rm || !slider || !ready()) return;
  void *rm = replay_of(slider);
  if (!rm) return;
  call(g_press, rm);
  g_held_rm = rm;
  debugPrintf("[uinav] replay slider held: the replay follows it while it is edited\n");
}
