/* rd_menu_hook.c -- menu navigation with a controller, glue (see rd_menu.h). MIT (see LICENSE).
 *
 * Runs from the main loop between engine frames (rd_menu_frame), so the il2cpp calls of the
 * scan never interleave with the game's C#. Everything it touches outside its own files is an
 * entry point: the pad state (nxp_pad_held), the synthetic finger (nx_input_hook_synth), the
 * driving state (rd_drive_active), the race state (its own RaceManager lookup, by name) and the
 * focus frame drawn from the eglSwapBuffers wrapper (rd_menu_draw) and the scene-load icall it
 * wraps (the frame goes the moment a load starts). With menu_navigation = 0
 * every entry point returns at once. Main thread. */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>
#include "rd_menu.h"
#include "rd_uinav.h"
#include "rd_ugui.h"
#include "rd_drive.h"
#include "rd_race.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_overlay.h"
#include "rd_platform.h"
#include "unity_input_hook.h"
#include "nx_pointer.h"
#include "config.h"
#include "util.h"

/* Screens with their own rule, from the debug_ui_nav survey of 5.0.8 on the console
 * (analysis/research/24). Other screens use the back-name heuristic and the top-left start. */
static const rd_menu_rule RULES[] = {
  /* pause: B resumes ("ButtonBackToMainMenu" would open the quit dialog) */
  { "ButtonResume", "ButtonResume", "ButtonResume", NULL, NULL },
  /* main menu: start on Quick Race; B = Escape = the game's EXIT dialog */
  { "ButtonQuickRace", NULL, "ButtonQuickRace", NULL, NULL },
  /* tabs on L / R: options (Basic / Advanced), player profile (Profile / Progression) */
  { "ToggleBasic", NULL, NULL, "ToggleBasic", "ToggleAdvanced" },
  { "ToggleProfile", NULL, NULL, "ToggleProfile", "ToggleProgression" },
  /* the garage's and the leaderboard's arrows are found by name (rd_menu_is_prev_name) */
  { NULL, NULL, NULL, NULL, NULL },      /* end */
};
#define NRULES ((int)(sizeof RULES / sizeof RULES[0]) - 1)

/* D-pad and the left stick's direction bits, per RD_UINAV_* direction. */
static const uint64_t DIR_BITS[] = {
  [RD_UINAV_LEFT]  = HidNpadButton_Left  | HidNpadButton_StickLLeft,
  [RD_UINAV_RIGHT] = HidNpadButton_Right | HidNpadButton_StickLRight,
  [RD_UINAV_UP]    = HidNpadButton_Up    | HidNpadButton_StickLUp,
  [RD_UINAV_DOWN]  = HidNpadButton_Down  | HidNpadButton_StickLDown,
};

static int g_state;                      /* 0 = not tried, 1 = ready, -1 = off */
static int g_active;                     /* the navigator ran this frame (the frame is drawn) */
static rd_ugui_target g_t[RD_UGUI_MAX_TARGETS];
static int g_n, g_focus = -1;
static uintptr_t g_focus_id;
static rd_uinav_rect g_focus_rect;
static int g_has_rect;
static uint32_t g_since_scan, g_signature;
static int g_dir = RD_UINAV_NONE;
static uint32_t g_dir_frames;
static uint64_t g_prev_held;
static int g_tap_frames;                 /* > 0 while the synthetic finger is down */
static int g_escape_next, g_escape_now;  /* Escape reported during the next engine frame */
static rd_menu_edit g_edit;              /* editing the focused slider */
static int g_step;                       /* L / R held: -1 / +1 */
static uint32_t g_step_frames;
static struct {                          /* a list scrolling one step (rd_menu_plan_move) */
  uintptr_t id;                          /* its ScrollRect, 0 = none */
  int vertical, dir, again;
  uint32_t frame;
  float from, to;
} g_scroll;
static const char *const DIR_NAMES[] = { "left", "right", "up", "down" };

/* ---- race state: RaceManager.pausePanel / isRaceEnded, looked up by name ------------- */
static const char *const RM_FIELDS[] = RD_RM_FIELD_NAMES;
enum { RM_PAUSE_PANEL, RM_IS_RACE_ENDED, RM_IS_PHOTO_MODE,
       RM_COUNT = sizeof RM_FIELDS / sizeof RM_FIELDS[0] };
static uint32_t g_rm[RM_COUNT];
static void *g_rm_type, *g_race_manager;
static void *g_loading_type;             /* LoadingScreenRandomizer */
static int g_loading;                    /* the loading screen was up at the last scan */
static uint32_t g_rm_frame;
static void *(*g_find_objects)(void *type);
static uint8_t (*g_active_self)(void *go);

/* ---- track preview: UIManager.introSequence / introSequenceStopped, USSequencer.IsPlaying */
static const char *const INTRO_FIELDS[] = RD_UIM_INTRO_FIELD_NAMES;
enum { INTRO_SEQUENCE, INTRO_STOPPED, INTRO_COUNT = sizeof INTRO_FIELDS / sizeof INTRO_FIELDS[0] };
static uint32_t g_intro[INTRO_COUNT];
static void *g_uim_type, *g_ui_manager;
static uint32_t g_uim_frame;
static struct { uint8_t (*fn)(void *self, const void *mi); const void *mi; } g_is_playing;

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))

/* Input.GetKeyDownInt wrapper: Escape is down during the frame after B found no back button. */
typedef uint8_t (*fn_key)(int32_t key);
static fn_key g_orig_key_down;
static uint8_t ic_key_down(int32_t key) {
  if (key == RD_MENU_KEY_ESCAPE && g_escape_now) return 1;
  return g_orig_key_down ? g_orig_key_down(key) : 0;
}

/* SceneManager's scene load wrapper: from the call on the frame is hidden and the menu
 * forgotten, until another scene is active (rd_menu_load_over). */
typedef void *(*fn_load_scene)(void *name, int32_t index, void *params, uint8_t complete_next);
static fn_load_scene g_orig_load_scene;
static void (*g_active_scene)(int32_t *handle);   /* GetActiveScene_Injected(out Scene) */
static int g_scene_loading;
static int32_t g_scene_before;
static uint32_t g_scene_frame;

/* The active scene's handle (Scene.m_Handle), 0 if unknown. */
static int32_t active_scene(void) {
  int32_t handle = 0;
  if (g_active_scene) g_active_scene(&handle);
  return handle;
}

static void *ic_load_scene(void *name, int32_t index, void *params, uint8_t complete_next) {
  if (g_state > 0) {
    g_scene_loading = 1;
    g_scene_before = active_scene();
    g_scene_frame = port_frame_count();
  }
  return g_orig_load_scene ? g_orig_load_scene(name, index, params, complete_next) : NULL;
}

static const rd_icall_override g_icalls[] = {
  { RD_ICALL_GET_KEY_DOWN, (void *)ic_key_down, (void **)&g_orig_key_down },
  { RD_ICALL_LOAD_SCENE, (void *)ic_load_scene, (void **)&g_orig_load_scene },
  { NULL, NULL, NULL },
};
const rd_icall_override *rd_menu_icall_table(void) { return g_icalls; }

int rd_menu_try_install(void) {
  if (g_state || !config.menu_navigation) return 1;
  const int r = rd_ugui_init();
  if (!r) return 0;
  g_state = r;
  void *rm = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_RACE_MANAGER);
  g_find_objects = g_il2cpp.resolve_icall(RD_ICALL_FIND_OBJECTS);
  g_active_self = g_il2cpp.resolve_icall(RD_ICALL_ACTIVE_SELF);
  g_active_scene = g_il2cpp.resolve_icall(RD_ICALL_ACTIVE_SCENE);
  if (!g_active_scene || !g_orig_load_scene)
    debugPrintf("[uinav] scene load not wrapped: the frame may stay up while a scene loads\n");
  if (rm && g_il2cpp.class_get_type) g_rm_type = g_il2cpp.type_get_object(g_il2cpp.class_get_type(rm));
  if (!rm || !g_rm_type || !g_find_objects || !g_active_self ||
      !rd_il2cpp_fields(&g_il2cpp, rm, RM_FIELDS, RM_COUNT, g_rm)) {
    g_rm_type = NULL;
    debugPrintf("[uinav] " RD_CLS_RACE_MANAGER " not found: no navigation on pause / results\n");
  }
  void *loading = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_LOADING_SCREEN);
  if (loading && g_find_objects && g_il2cpp.class_get_type)
    g_loading_type = g_il2cpp.type_get_object(g_il2cpp.class_get_type(loading));
  if (!g_loading_type)
    debugPrintf("[uinav] " RD_CLS_LOADING_SCREEN " not found: the frame may show while loading\n");
  void *uim = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_UI_MANAGER);
  void *seq = rd_il2cpp_class(&g_il2cpp, RD_ASM_USEQ, RD_NS_USEQ, RD_CLS_USEQ);
  void **playing = seq ? g_il2cpp.class_get_method_from_name(seq, RD_MTH_IS_PLAYING, 0) : NULL;
  if (uim && playing && playing[0] && g_find_objects && g_il2cpp.class_get_type &&
      rd_il2cpp_fields(&g_il2cpp, uim, INTRO_FIELDS, INTRO_COUNT, g_intro)) {
    g_uim_type = g_il2cpp.type_get_object(g_il2cpp.class_get_type(uim));
    g_is_playing.fn = (uint8_t (*)(void *, const void *))playing[0];
    g_is_playing.mi = playing;
  } else {
    debugPrintf("[uinav] " RD_CLS_UI_MANAGER " intro not found: the track preview is skipped by touch only\n");
  }
  debugPrintf("[uinav] menu navigation %s (select 0x%llx, back 0x%llx)\n",
              g_state > 0 ? "on" : "OFF", (unsigned long long)config.ui_select,
              (unsigned long long)config.ui_back);
  return 1;
}

static void *find_one(void *type, void **cache, uint32_t *frame);

/* Pause menu open / race over, from this race's RaceManager (found again now and then). */
static void race_state(int *paused, int *ended) {
  *paused = *ended = 0;
  if (!g_rm_type || !rd_race_active()) { g_race_manager = NULL; return; }
  if (!find_one(g_rm_type, &g_race_manager, &g_rm_frame)) return;
  void *panel = FIELD(g_race_manager, g_rm[RM_PAUSE_PANEL], void *);
  *paused = rd_ugui_alive(panel) && g_active_self(panel);
  *ended = FIELD(g_race_manager, g_rm[RM_IS_RACE_ENDED], uint8_t);
}

/* An object of `type` in the scene, looked up again every RD_MENU_RACE_FRAMES (*cache, *frame)
 * and as soon as the cached one is destroyed (a scene change); NULL if there is none. */
static void *find_one(void *type, void **cache, uint32_t *frame) {
  const uint32_t now = port_frame_count();
  if (!rd_ugui_alive(*cache) || now - *frame >= RD_MENU_RACE_FRAMES) {   /* destroyed: find again */
    void *arr = g_find_objects(type);
    *cache = arr && g_il2cpp.array_length(arr) ? rd_il2cpp_array_at(arr, 0) : NULL;
    *frame = now;
  }
  if (!rd_ugui_alive(*cache)) *cache = NULL;
  return *cache;
}

/* 1 while the track preview before a race plays (and a touch would skip it). */
static int intro_playing(void) {
  if (!g_uim_type || !rd_race_active()) { g_ui_manager = NULL; return 0; }
  char *uim = find_one(g_uim_type, &g_ui_manager, &g_uim_frame);
  if (!uim || FIELD(uim, g_intro[INTRO_STOPPED], uint8_t)) return 0;
  void *seq = FIELD(uim, g_intro[INTRO_SEQUENCE], void *);
  return rd_ugui_alive(seq) && g_is_playing.fn(seq, g_is_playing.mi);
}

/* Rejected Selectables of the current scan, logged with debug_ui_nav when the screen is new. */
#define MAX_REJECTS 48
static struct { char name[RD_UGUI_NAME], why[20], hit[RD_UGUI_NAME]; } g_rej[MAX_REJECTS];
static int g_nrej;
static void on_reject(const char *name, const char *why, const char *hit) {
  if (g_nrej >= MAX_REJECTS) return;
  snprintf(g_rej[g_nrej].name, sizeof g_rej[0].name, "%s", name);
  snprintf(g_rej[g_nrej].why, sizeof g_rej[0].why, "%s", why);
  snprintf(g_rej[g_nrej].hit, sizeof g_rej[0].hit, "%s", hit);
  g_nrej++;
}

/* Logs the targets of a screen not seen just before (debug_ui_nav). */
static void log_screen(void) {
  const uint32_t sig = rd_menu_signature(g_t, g_n);
  if (sig == g_signature) return;
  g_signature = sig;
  if (!config.debug_ui_nav) return;
  debugPrintf("[uinav] screen: %d targets\n", g_n);
  for (int i = 0; i < g_n; i++)
    debugPrintf("[uinav]   %-28s %-10s in %-24s at %4.0f,%4.0f %4.0fx%-4.0f%s\n", g_t[i].name,
                g_t[i].cls, g_t[i].parent, g_t[i].t.r.x, g_t[i].t.r.y, g_t[i].t.r.w,
                g_t[i].t.r.h, rd_menu_is_back_name(g_t[i].name) ? "  (back)" : "");
  for (int i = 0; i < g_nrej; i++)
    debugPrintf("[uinav]   - %-26s %s %s\n", g_rej[i].name, g_rej[i].why, g_rej[i].hit);
}

/* 1 if the loading screen is up (FindObjectsOfType returns active objects only). */
static int loading_screen(void) {
  void *arr = g_loading_type ? g_find_objects(g_loading_type) : NULL;
  return arr && g_il2cpp.array_length(arr) > 0;
}

/* Re-reads the targets and keeps the focus on the same element (or the nearest one). */
static void scan(void) {
  g_since_scan = 0;
  g_nrej = 0;
  g_n = rd_ugui_scan(g_t, RD_UGUI_MAX_TARGETS, screen_width, screen_height,
                     config.debug_ui_nav ? on_reject : NULL);
  log_screen();
  static rd_uinav_target flat[RD_UGUI_MAX_TARGETS];
  for (int i = 0; i < g_n; i++) flat[i] = g_t[i].t;
  const int had = g_focus_id != 0;
  g_focus = rd_uinav_refocus(flat, g_n, g_focus_id, &g_focus_rect, g_has_rect);
  if (!had || (g_focus >= 0 && g_t[g_focus].t.id != g_focus_id)) {   /* a new screen */
    const rd_menu_rule *rule = rd_menu_rule_for(RULES, NRULES, g_t, g_n);
    const int first = rule ? rd_menu_find(g_t, g_n, rule->first) : -1;
    if (first >= 0) g_focus = first;
  }
  if (g_edit.on && (g_focus < 0 || g_t[g_focus].t.id != g_focus_id ||
                    g_t[g_focus].kind != RD_UGUI_SLIDER))
    g_edit.on = 0;                       /* the edited slider is gone */
  if (g_focus >= 0) {
    g_focus_id = g_t[g_focus].t.id;
    g_focus_rect = g_t[g_focus].t.r;
    g_has_rect = 1;
  }
}

/* Puts the synthetic finger down at (x, y), screen px, top-left origin. */
static void tap_at(float x, float y) {
  nx_input_hook_synth(1, x, (float)screen_height - y);
  g_tap_frames = 1;
}

/* Taps target i where a tap reaches it (rd_ugui_target.tap_x / tap_y). */
static void tap(int i) {
  tap_at(g_t[i].tap_x, g_t[i].tap_y);
}

/* A frame while the focused slider is edited: the direction along it changes the value (with
 * auto-repeat), A keeps it, B restores the value from before. */
static void edit_frame(int dir, uint64_t pressed) {
  if (!rd_ugui_alive((void *)g_focus_id)) { g_edit.on = 0; return; }   /* the slider is gone */
  if (pressed & config.ui_select) { rd_menu_edit_end(&g_edit, 0); return; }
  if (pressed & config.ui_back) { rd_ugui_slider_set(g_focus_id, rd_menu_edit_end(&g_edit, 1)); return; }
  if (dir == RD_UINAV_NONE || !rd_uinav_repeat(g_dir_frames)) return;
  rd_ugui_slider s;
  rd_ugui_slider_get(g_focus_id, &s);
  const int delta = rd_menu_edit_delta(dir, s.direction);
  if (delta) rd_ugui_slider_set(g_focus_id, rd_menu_edit_move(&g_edit, delta));
}

/* Moves the focus in `dir`; may_scroll = 1 lets a list scroll when the focus reaches its edge
 * (rd_menu_plan_move). */
static void move(int dir, int may_scroll) {
  static rd_uinav_target flat[RD_UGUI_MAX_TARGETS];
  for (int i = 0; i < g_n; i++) flat[i] = g_t[i].t;
  rd_ugui_scroll s;
  const uintptr_t list = g_t[g_focus].scroll;
  const int have = may_scroll && list && rd_ugui_scroll_get(list, screen_height, &s);
  const rd_menu_move mv = rd_menu_plan_move(g_t, flat, g_n, g_focus, dir, have ? &s : NULL);
  if (config.debug_ui_nav)
    debugPrintf("[uinav] %s: %s -> %s\n", DIR_NAMES[dir], g_t[g_focus].name,
                mv.focus >= 0 ? g_t[mv.focus].name : "(nothing)");
  if (mv.focus >= 0) {
    g_focus = mv.focus;
    g_focus_id = g_t[mv.focus].t.id;
    g_focus_rect = g_t[mv.focus].t.r;
  }
  if (!mv.scroll) return;
  const float from = mv.vertical ? s.y : s.x;
  if (config.debug_ui_nav)
    debugPrintf("[uinav] scroll %c %.3f -> %.3f%s\n", mv.vertical ? 'y' : 'x', (double)from,
                (double)mv.to, mv.again ? ", then move again" : "");
  g_scroll.id = list;
  g_scroll.vertical = mv.vertical;
  g_scroll.dir = dir;
  g_scroll.again = mv.again;
  g_scroll.frame = 0;
  g_scroll.from = from;
  g_scroll.to = mv.to;
}

/* A frame of a list scrolling: eased over RD_MENU_SCROLL_FRAMES; one frame after the last
 * step (the engine has laid the new cells out and scan() has seen them) the move that
 * scrolled is made again, without scrolling. */
static void scroll_frame(void) {
  if (!rd_ugui_alive((void *)g_scroll.id)) { g_scroll.id = 0; return; }   /* the list is gone */
  if (++g_scroll.frame <= RD_MENU_SCROLL_FRAMES) {
    const float k = (float)g_scroll.frame / (float)RD_MENU_SCROLL_FRAMES;
    const float ease = k * k * (3.0f - 2.0f * k);                         /* smoothstep */
    rd_ugui_scroll_set(g_scroll.id, g_scroll.vertical,
                       g_scroll.from + (g_scroll.to - g_scroll.from) * ease);
    return;
  }
  g_scroll.id = 0;
  if (g_scroll.again && g_focus >= 0) move(g_scroll.dir, 0);
}

/* The direction held this frame (first of left/right/up/down), or RD_UINAV_NONE. */
static int held_dir(uint64_t held) {
  for (int d = RD_UINAV_LEFT; d <= RD_UINAV_DOWN; d++)
    if (held & DIR_BITS[d]) return d;
  return RD_UINAV_NONE;
}

void rd_menu_frame(void) {
  if (g_state <= 0) return;
  g_escape_now = g_escape_next;            /* one engine frame, then gone */
  g_escape_next = 0;
  if (g_tap_frames && ++g_tap_frames > RD_MENU_TAP_FRAMES) {
    nx_input_hook_synth(0, 0.0f, 0.0f);    /* lift: the game sees the tap complete */
    g_tap_frames = 0;
    g_since_scan = RD_MENU_SCAN_FRAMES;    /* the screen probably changed: rescan now */
  }
  int paused, ended;
  race_state(&paused, &ended);
  const uint64_t held = nxp_pad_held();
  const uint64_t pressed = held & ~g_prev_held;
  g_prev_held = held;
  if (!g_tap_frames && (pressed & (config.ui_select | config.ui_back)) && intro_playing()) {
    tap_at(0.5f * (float)screen_width, 0.5f * (float)screen_height);   /* skip the preview */
    return;
  }
  static uint32_t loading_checked;
  if (g_scene_loading && rd_menu_load_over(g_scene_before, active_scene(),
                                           port_frame_count() - g_scene_frame)) {
    g_scene_loading = 0;
    loading_checked = port_frame_count() - RD_MENU_SCAN_FRAMES;   /* look for the loading screen now */
  }
  if (g_scene_loading) {
    g_loading = 1;
  } else if (port_frame_count() - loading_checked >= RD_MENU_SCAN_FRAMES) {
    g_loading = loading_screen();
    loading_checked = port_frame_count();
  }
  g_active = rd_menu_wanted(1, g_loading, rd_drive_active(), paused, ended);
  if (!g_active) {
    g_dir = RD_UINAV_NONE;
    if (g_loading) {                       /* forget the menu: after loading a new screen starts */
      g_focus = -1;
      g_focus_id = 0;
      g_has_rect = 0;
      g_edit.on = 0;
      g_scroll.id = 0;
      g_race_manager = g_ui_manager = NULL;   /* the race scene goes away with the loading */
      g_since_scan = RD_MENU_SCAN_FRAMES;  /* rescan as soon as the loading is over */
    }
    return;
  }

  if (g_scroll.id || ++g_since_scan >= RD_MENU_SCAN_FRAMES) scan();   /* every frame while scrolling */
  if (g_tap_frames) return;                /* one action at a time */
  if (g_scroll.id) { scroll_frame(); return; }

  const int dir = held_dir(held);
  g_dir_frames = dir == g_dir ? g_dir_frames + 1 : 1;
  g_dir = dir;
  if (g_edit.on) {
    edit_frame(dir, pressed);
    return;
  }
  if (dir != RD_UINAV_NONE && g_focus >= 0 && rd_uinav_repeat(g_dir_frames)) {
    move(dir, 1);
    if (g_scroll.id) return;               /* the list moves first */
  }
  const int step = (held & config.ui_prev) ? -1 : (held & config.ui_next) ? 1 : 0;
  g_step_frames = step == g_step ? g_step_frames + 1 : 1;
  g_step = step;
  if (step && rd_uinav_repeat(g_step_frames)) {      /* previous / next car, tab, track */
    const int s = rd_menu_step_target(g_t, g_n, rd_menu_rule_for(RULES, NRULES, g_t, g_n), step);
    if (s >= 0) { tap(s); return; }
  }
  if ((pressed & config.ui_select) && g_focus >= 0 && g_t[g_focus].kind == RD_UGUI_SLIDER &&
      rd_ugui_alive((void *)g_focus_id)) {
    rd_ugui_slider s;
    rd_ugui_slider_get(g_focus_id, &s);
    rd_menu_edit_begin(&g_edit, s.value, s.min, s.max, s.whole);
  } else if ((pressed & config.ui_select) && g_focus >= 0) {
    tap(g_focus);
  } else if (pressed & config.ui_back) {
    const int b = rd_menu_back_target(g_t, g_n, rd_menu_rule_for(RULES, NRULES, g_t, g_n));
    if (b >= 0) tap(b);
    else g_escape_next = 1;
  }
}

void rd_menu_draw(void) {
  if (g_state <= 0 || !g_active || g_focus < 0 || g_scene_loading) return;
  const float px = RD_MENU_FRAME_PX_720 * (float)screen_height / 720.0f *
                   (g_edit.on ? RD_MENU_EDIT_PX_SCALE : 1.0f);
  const rd_uinav_rect r = g_focus_rect;
  const rd_rect sides[4] = {
    { r.x - px, r.y - px, r.w + 2.0f * px, px },     /* top */
    { r.x - px, r.y + r.h, r.w + 2.0f * px, px },    /* bottom */
    { r.x - px, r.y, px, r.h },                      /* left */
    { r.x + r.w, r.y, px, r.h },                     /* right */
  };
  if (!rd_overlay_begin(screen_width, screen_height)) return;
  if (g_edit.on) rd_overlay_rects(sides, 4, RD_MENU_EDIT_RGBA);
  else rd_overlay_rects(sides, 4, RD_MENU_FRAME_RGBA);
  rd_overlay_end();
}
