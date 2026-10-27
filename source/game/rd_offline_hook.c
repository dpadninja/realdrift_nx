/* rd_offline_hook.c -- offline menus, il2cpp glue (see rd_offline.h). MIT (see LICENSE).
 *
 * Runs from the main loop between engine frames (rd_offline_frame), so the icalls never
 * interleave with the game's C#. The scene roots (MenuManager, RaceManager) and PersistentData
 * are found with FindObjectsOfType; each target is found once per root with
 * Transform.FindRelativeTransformWithPath (inactive objects included) and kept. Anything kept
 * across frames is checked with rd_ugui_alive first: an icall on a destroyed object throws
 * under our C frames, where nothing can catch it. Main thread. */
#include <stdint.h>
#include "rd_offline.h"
#include "rd_ugui.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "config.h"
#include "util.h"

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))

typedef struct { void *fn; const void *mi; } method;
typedef struct { float x, y; } rd_vec2;             /* UnityEngine.Vector2 */

static int g_state;                              /* 0 = not tried, 1 = ready, -1 = off */
static const rd_offline_target *g_targets;
static int g_n;
static struct {
  void *(*find_objects)(void *type);
  void *(*transform)(void *component);
  void *(*find_path)(void *transform, void *path, uint8_t active_only);
  void *(*game_object)(void *component);
  void *(*get_component)(void *go, void *type);
  uint8_t (*active_self)(void *go);
  void (*set_active)(void *go, uint8_t value);
  uint8_t (*get_enabled)(void *behaviour);
  void (*set_enabled)(void *behaviour, uint8_t value);
  void (*get_anchored)(void *rect_transform, rd_vec2 *out);
  void (*set_anchored)(void *rect_transform, const rd_vec2 *value);
} ic;
static method g_get_text, g_set_text;
static void *g_input_type, *g_text_type;
static void *g_label;                            /* RD_OFFLINE_STATISTICS, a managed string */
static uint32_t g_lb_type_off, g_lb_mode_off;    /* PersistentData.leaderboardType / RaceMode */

/* A scene object of one type, looked up again when it is gone. */
typedef struct {
  void *type;                                    /* System.Type */
  void *obj;                                     /* the component, NULL = not found */
  uint32_t frame;                                /* when it was last looked for */
} found;
static found g_root[RD_OFFLINE_ROOTS], g_persistent;
static void *g_target[RD_OFFLINE_MAX_TARGETS];   /* per target: GameObject or component */
static float g_target_x[RD_OFFLINE_MAX_TARGETS];   /* RD_OFFLINE_MOVE_X: where it goes */

/* System.Type of a class, or NULL. */
static void *type_of(const char *assembly, const char *ns, const char *name) {
  void *klass = rd_il2cpp_class(&g_il2cpp, assembly, ns, name);
  return klass ? g_il2cpp.type_get_object(g_il2cpp.class_get_type(klass)) : NULL;
}

/* A managed method of TMP_Text, or {NULL, NULL}. */
static method text_method(const char *name, int argc) {
  void *klass = rd_il2cpp_class(&g_il2cpp, RD_ASM_TMP, RD_NS_TMP, RD_CLS_TMP_TEXT);
  void **mi = klass ? g_il2cpp.class_get_method_from_name(klass, name, argc) : NULL;
  return mi && mi[0] ? (method){ mi[0], mi } : (method){ NULL, NULL };
}

/* Resolves everything once. 1 = ready, 0 = the game is not loaded yet, -1 = off. */
static int install(void) {
  const int ugui = rd_ugui_init();               /* rd_ugui_alive needs it */
  if (ugui <= 0) return ugui;
  void *pd = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_PERSISTENT_DATA);
  if (!pd) return 0;
  ic.find_objects  = g_il2cpp.resolve_icall(RD_ICALL_FIND_OBJECTS);
  ic.transform     = g_il2cpp.resolve_icall(RD_ICALL_TRANSFORM);
  ic.find_path     = g_il2cpp.resolve_icall(RD_ICALL_FIND_PATH);
  ic.game_object   = g_il2cpp.resolve_icall(RD_ICALL_GAME_OBJECT);
  ic.get_component = g_il2cpp.resolve_icall(RD_ICALL_GET_COMPONENT);
  ic.active_self   = g_il2cpp.resolve_icall(RD_ICALL_ACTIVE_SELF);
  ic.set_active    = g_il2cpp.resolve_icall(RD_ICALL_SET_ACTIVE);
  ic.get_enabled   = g_il2cpp.resolve_icall(RD_ICALL_GET_ENABLED);
  ic.set_enabled   = g_il2cpp.resolve_icall(RD_ICALL_SET_ENABLED);
  ic.get_anchored  = g_il2cpp.resolve_icall(RD_ICALL_GET_ANCHORED);
  ic.set_anchored  = g_il2cpp.resolve_icall(RD_ICALL_SET_ANCHORED);
  g_get_text = text_method(RD_MTH_GET_TEXT, 0);
  g_set_text = text_method(RD_MTH_SET_TEXT, 1);
  g_input_type  = type_of(RD_ASM_TMP, RD_NS_TMP, RD_CLS_TMP_INPUT);
  g_text_type   = type_of(RD_ASM_TMP, RD_NS_TMP, RD_CLS_TMP_TEXT);
  g_root[RD_OFFLINE_MENU].type = type_of(RD_ASM_GAME, RD_NS_GAME, RD_CLS_MENU_MANAGER);
  g_root[RD_OFFLINE_RACE].type = type_of(RD_ASM_GAME, RD_NS_GAME, RD_CLS_RACE_MANAGER);
  g_persistent.type = type_of(RD_ASM_GAME, RD_NS_GAME, RD_CLS_PERSISTENT_DATA);
  g_label = g_il2cpp.string_new(RD_OFFLINE_STATISTICS);
  if (g_label) g_il2cpp.gchandle_new(g_label, 1);               /* kept: never collected */
  if (!ic.find_objects || !ic.transform || !ic.find_path || !ic.game_object ||
      !ic.get_component || !ic.active_self || !ic.set_active || !ic.get_enabled ||
      !ic.set_enabled || !ic.get_anchored || !ic.set_anchored || !g_get_text.fn || !g_set_text.fn ||
      !g_input_type || !g_text_type || !g_root[RD_OFFLINE_MENU].type ||
      !g_root[RD_OFFLINE_RACE].type || !g_persistent.type || !g_label ||
      !rd_il2cpp_field_offset(&g_il2cpp, pd, RD_FLD_LEADERBOARD_TYPE, &g_lb_type_off) ||
      !rd_il2cpp_field_offset(&g_il2cpp, pd, RD_FLD_LEADERBOARD_MODE, &g_lb_mode_off)) {
    debugPrintf("[offline] a name was not found: online features stay in the menus\n");
    return -1;
  }
  g_targets = rd_offline_targets(&g_n);
  if (g_n > RD_OFFLINE_MAX_TARGETS) g_n = RD_OFFLINE_MAX_TARGETS;
  debugPrintf("[offline] online features hidden (%d targets)\n", g_n);
  return 1;
}

/* The object the action of target i works on, below the root's transform; NULL if absent.
 * A moved one's destination is taken from where the scene put it. */
static void *resolve(int i, void *root_tr) {
  const rd_offline_target *t = &g_targets[i];
  void *path = g_il2cpp.string_new(t->path);
  void *tr = path ? ic.find_path(root_tr, path, 0) : NULL;
  void *go = tr ? ic.game_object(tr) : NULL;
  if (!go) return NULL;
  switch (t->action) {
    case RD_OFFLINE_HIDE:      return go;
    case RD_OFFLINE_MOVE_X: {
      rd_vec2 at;
      ic.get_anchored(tr, &at);
      g_target_x[i] = at.x + t->dx;
      return tr;                                 /* a uGUI Transform is the RectTransform */
    }
    case RD_OFFLINE_NO_INPUT:  return ic.get_component(go, g_input_type);
    case RD_OFFLINE_LABEL:     return ic.get_component(go, g_text_type);
  }
  return NULL;
}

/* f.obj: kept while alive, else looked for again every RD_OFFLINE_FIND_FRAMES. 1 if the
 * object was found just now. */
static int refind(found *f) {
  if (rd_ugui_alive(f->obj)) return 0;
  f->obj = NULL;
  const uint32_t now = port_frame_count();
  if (f->frame && now - f->frame < RD_OFFLINE_FIND_FRAMES) return 0;
  f->frame = now;
  void *arr = ic.find_objects(f->type);
  f->obj = arr && g_il2cpp.array_length(arr) ? rd_il2cpp_array_at(arr, 0) : NULL;
  return f->obj != NULL;
}

/* Finds the root's targets again (a new scene). */
static void resolve_root(rd_offline_root r) {
  void *tr = ic.transform(g_root[r].obj);
  int wanted = 0, got = 0;
  for (int i = 0; i < g_n; i++) {
    if (g_targets[i].root != r) continue;
    g_target[i] = NULL;
    if (!rd_offline_wants(&g_targets[i], config.switch_name)) continue;
    g_target[i] = resolve(i, tr);
    wanted++;
    if (g_target[i]) got++;
    else debugPrintf("[offline] not found: %s\n", g_targets[i].path);
  }
  debugPrintf("[offline] %s: %d/%d targets\n", r == RD_OFFLINE_MENU ? "menu" : "race", got,
              wanted);
}

/* Applies target i's action if the game undid it. */
static void apply(int i) {
  void *obj = g_target[i];
  if (!rd_ugui_alive(obj)) return;
  switch (g_targets[i].action) {
    case RD_OFFLINE_HIDE:
      if (ic.active_self(obj)) ic.set_active(obj, 0);
      break;
    case RD_OFFLINE_MOVE_X: {
      rd_vec2 at;
      ic.get_anchored(obj, &at);
      if (at.x != g_target_x[i]) {
        at.x = g_target_x[i];
        ic.set_anchored(obj, &at);
      }
      break;
    }
    case RD_OFFLINE_NO_INPUT:
      if (ic.get_enabled(obj)) ic.set_enabled(obj, 0);
      break;
    case RD_OFFLINE_LABEL: {
      void *now = ((void *(*)(void *, const void *))g_get_text.fn)(obj, g_get_text.mi);
      if (!now || !rd_il2cpp_str_eq(&g_il2cpp, now, RD_OFFLINE_STATISTICS))
        ((void (*)(void *, void *, const void *))g_set_text.fn)(obj, g_label, g_set_text.mi);
      break;
    }
  }
}

void rd_offline_frame(void) {
  if (!config.offline_ui || g_state < 0) return;
  if ((port_frame_count() % RD_OFFLINE_FRAMES) != 0) return;
  if (!g_state) {
    g_state = install();
    if (g_state <= 0) return;
  }
  for (int r = 0; r < RD_OFFLINE_ROOTS; r++) {
    if (refind(&g_root[r])) resolve_root((rd_offline_root)r);
    if (!g_root[r].obj) continue;
    for (int i = 0; i < g_n; i++)
      if (g_targets[i].root == (rd_offline_root)r) apply(i);
  }
  /* The online boards cannot load: the statistics screen always shows the device's own,
   * sorted by points (rd_offline.h). Only in the menu: races leave the fields alone. */
  refind(&g_persistent);
  if (g_persistent.obj && g_root[RD_OFFLINE_MENU].obj) {
    FIELD(g_persistent.obj, g_lb_type_off, int32_t) = RD_LEADERBOARD_LOCAL;
    FIELD(g_persistent.obj, g_lb_mode_off, int32_t) = RD_RACE_MODE_DRIFT;
  }
}
