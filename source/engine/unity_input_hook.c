/* unity_input_hook.c -- the Switch touch / cursor state as UnityEngine.Input sees it.
 *
 * Unity's own Android input path (nativeInjectEvent) accepts events but never updates
 * Input.touchCount / GetTouch / mousePosition here, so the port replaces those icalls by name
 * when libunity registers them (rd_icall.h, table g_input_icalls below) and answers them from
 * the finger state android_native_feed_hid pushes every frame (nx_input_hook_update_multi).
 * UnityEngine.Touch's layout (0x44 bytes) is checked against dump.cs (NxTouch below).
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "unity_input_hook.h"

int   debugPrintf(char *fmt, ...);
extern int screen_width, screen_height;

/* ---- touch state, written by android_native_feed_hid every frame ----------
 * Multi-touch: g_hook_touch[0..g_hook_count) are the fingers Unity will see this frame. The
 * mouse-emulation globals mirror the PRIMARY finger so the GetMouseButton* hooks and any
 * mouse-driven UI keep working exactly as before. */
typedef struct { int id; int slot; float x, y; int phase; } HookTouch;
/* id   = raw Switch HID finger_id -- used ONLY to match a finger against last frame.
 * slot = the DENSE 0-based index we report as UnityEngine.Touch.fingerId.
 *
 * These must be kept apart. Horizon assigns HID finger_ids that climb as new fingers
 * land (they are not re-densified), whereas Android's Unity hands out dense 0-based
 * fingerIds. Games use fingerId as an ARRAY INDEX (Bad Piggies did, where a raw HID id threw
 * IndexOutOfRangeException every frame a finger was down and killed touch input). A finger keeps its slot for as long as it stays down; the slot
 * is released only after its Ended frame has been published. */
static HookTouch g_hook_touch[NX_MAX_TOUCH];

/* slot allocator: slot_hid[s] = the HID finger_id holding slot s, or -1 if free */
static int  slot_hid[NX_MAX_TOUCH];
static int  slot_ready = 0;
static void slot_init(void) {
  if (slot_ready) return;
  for (int s = 0; s < NX_MAX_TOUCH; s++) slot_hid[s] = -1;
  slot_ready = 1;
}
static int slot_find(int hid) {
  for (int s = 0; s < NX_MAX_TOUCH; s++) if (slot_hid[s] == hid) return s;
  return -1;
}
static int slot_take(int hid) {                    /* lowest free slot, like Android */
  for (int s = 0; s < NX_MAX_TOUCH; s++) if (slot_hid[s] < 0) { slot_hid[s] = hid; return s; }
  return -1;
}

int   g_hook_count = 0;                 /* Input.touchCount (0..NX_MAX_TOUCH)            */
int   g_hook_btn   = 0;                 /* Input.GetMouseButton(0)  (primary finger)     */
int   g_hook_btn_down = 0, g_hook_btn_up = 0;  /* GetMouseButtonDown/Up(0), 1-frame edges */
int   g_hook_phase = 3;                 /* primary finger's TouchPhase                   */
float g_hook_x = 0.0f, g_hook_y = 0.0f; /* Unity screen space (bottom-left origin, px)   */

/* ---- Unity value types (AArch64 return conventions matter) ---------------- */
typedef struct { float x, y; }    NxV2;
typedef struct { float x, y, z; } NxV3;                 /* HFA -> s0,s1,s2      */
typedef struct {                                        /* UnityEngine.Touch, 0x44 bytes */
  int32_t m_FingerId;       NxV2 m_Position;   NxV2 m_RawPosition; NxV2 m_PositionDelta;
  float   m_TimeDelta;      int32_t m_TapCount; int32_t m_Phase;   int32_t m_Type;
  float   m_Pressure;       float m_maxPressure; float m_Radius;   float m_RadiusVariance;
  float   m_AltitudeAngle;  float m_AzimuthAngle;
} NxTouch;                                               /* > 16 bytes -> sret (x8) */

/* One place that turns our finger state into a UnityEngine.Touch, so Input.GetTouch and
 * Input.touches can never disagree about what a touch looks like. */
static void fill_touch(NxTouch *t, const HookTouch *h) {
  memset(t, 0, sizeof *t);
  t->m_FingerId   = h->slot;                   /* DENSE 0-based id (see HookTouch) -- never the raw HID id */
  t->m_Position.x = h->x; t->m_Position.y = h->y;
  t->m_RawPosition = t->m_Position;
  t->m_Phase = h->phase; t->m_TapCount = 1;
  t->m_Pressure = 1.0f; t->m_maxPressure = 1.0f;
}

/* ---- icall replacements (registered by name, see rd_icall.h) -------------
 * Icall ABI = plain C with the managed args, no MethodInfo. In Unity 2018.4 the managed
 * Input.touches / Input.GetTouch / Input.mousePosition are thin wrappers over these, so
 * replacing the icalls covers every managed API without any per-build RVA. */
static int32_t ic_touchCount(void)                 { return g_hook_count; }
static void    ic_GetTouch(int32_t i, NxTouch *ret){
  if (!ret) return;
  memset(ret, 0, sizeof *ret);
  if (i >= 0 && i < g_hook_count) fill_touch(ret, &g_hook_touch[i]);
}
static void    ic_mousePosition(NxV3 *ret)         { if (ret) { ret->x = g_hook_x; ret->y = g_hook_y; ret->z = 0.0f; } }
static uint8_t ic_GetMouseButton(int32_t b)        { return (b==0) ? (uint8_t)g_hook_btn : 0; }
static uint8_t ic_GetMouseButtonDown(int32_t b)    { return (b==0) ? (uint8_t)g_hook_btn_down : 0; }
static uint8_t ic_GetMouseButtonUp(int32_t b)      { return (b==0) ? (uint8_t)g_hook_btn_up : 0; }
static uint8_t ic_true(void)                       { return 1; }

static const rd_icall_override g_input_icalls[] = {
  { "UnityEngine.Input::get_touchCount",             (void *)ic_touchCount, NULL },
  { "UnityEngine.Input::GetTouch_Injected",          (void *)ic_GetTouch, NULL },
  { "UnityEngine.Input::get_mousePosition_Injected", (void *)ic_mousePosition, NULL },
  { "UnityEngine.Input::GetMouseButton",             (void *)ic_GetMouseButton, NULL },
  { "UnityEngine.Input::GetMouseButtonDown",         (void *)ic_GetMouseButtonDown, NULL },
  { "UnityEngine.Input::GetMouseButtonUp",           (void *)ic_GetMouseButtonUp, NULL },
  { "UnityEngine.Input::get_touchSupported",         (void *)ic_true, NULL },
  { NULL, NULL, NULL },
};
const rd_icall_override *nx_input_icall_table(void) { return g_input_icalls; }

/* Called by android_native_feed_hid with EVERY finger currently down, already mapped to Unity
 * screen space (bottom-left origin, game pixels). Phases are derived by matching HID finger ids
 * against last frame: a new id is Began, a known id is Moved (or Stationary if it didn't move),
 * and an id that disappeared is reported for exactly ONE more frame as Ended at its last
 * position -- Unity's contract, and what lets the game see a tap complete.
 * Fingers keep their slot order, so Input.GetTouch(i) is stable across frames. */
void nx_input_hook_update_multi(const NxTouchIn *in, int n) {
  static HookTouch prev[NX_MAX_TOUCH];
  static int prev_n = 0;
  HookTouch cur[NX_MAX_TOUCH];
  int cn = 0;

  slot_init();
  if (n < 0) n = 0;
  if (n > NX_MAX_TOUCH) n = NX_MAX_TOUCH;

  /* 1) every finger that is down now: Began / Moved / Stationary */
  for (int i = 0; i < n; i++) {
    const HookTouch *was = NULL;
    for (int j = 0; j < prev_n; j++)
      if (prev[j].id == in[i].id && prev[j].phase != 3) { was = &prev[j]; break; }

    /* keep this finger's slot if it already has one, else take the lowest free slot */
    int s = slot_find(in[i].id);
    if (s < 0) s = slot_take(in[i].id);
    if (s < 0) continue;                 /* all slots busy: drop the extra finger */

    cur[cn].id   = in[i].id;
    cur[cn].slot = s;
    cur[cn].x  = in[i].x;
    cur[cn].y  = in[i].y;
    if (was) {
      float dx = in[i].x - was->x, dy = in[i].y - was->y;
      cur[cn].phase = (dx*dx + dy*dy > 0.25f) ? 1 /*Moved*/ : 2 /*Stationary*/;
    } else {
      cur[cn].phase = 0 /*Began*/;
    }
    cn++;
  }

  /* 2) fingers that were down last frame and are now gone: one Ended frame each */
  for (int j = 0; j < prev_n && cn < NX_MAX_TOUCH; j++) {
    if (prev[j].phase == 3) continue;              /* already reported Ended -> drop it */
    int still_down = 0;
    for (int i = 0; i < n; i++)
      if (in[i].id == prev[j].id) { still_down = 1; break; }
    if (!still_down) {
      cur[cn] = prev[j];
      cur[cn].phase = 3 /*Ended*/;
      cn++;
    }
  }

  /* 3) publish to the hooks */
  for (int i = 0; i < cn; i++) g_hook_touch[i] = cur[i];
  g_hook_count = cn;

  /* 3b) a finger that just reported Ended is gone -- free its slot so the next finger
   *     down reuses the lowest index, exactly as Android does. Done AFTER publishing,
   *     so the Ended frame still carries the fingerId the game saw while it was down. */
  for (int i = 0; i < cn; i++)
    if (cur[i].phase == 3 && cur[i].slot >= 0 && cur[i].slot < NX_MAX_TOUCH)
      slot_hid[cur[i].slot] = -1;

  { static int logged = 0;
    if (!logged && cn > 0) { logged = 1;
      debugPrintf("[input] first touch: HID finger_id=%d -> Touch.fingerId=%d (densified)\n",
                  cur[0].id, cur[0].slot); } }

  /* 4) mouse emulation mirrors the primary finger (unchanged behaviour for mouse-driven UI) */
  static int prev_active = 0;
  int active = (n > 0);
  g_hook_btn_down = (active && !prev_active);
  g_hook_btn_up   = (!active && prev_active);
  g_hook_btn      = active;
  if (cn > 0) {
    g_hook_x = cur[0].x; g_hook_y = cur[0].y;
    g_hook_phase = cur[0].phase;
  } else {
    g_hook_phase = 3;
  }
  prev_active = active;

  /* 5) remember this frame */
  for (int i = 0; i < cn; i++) prev[i] = cur[i];
  prev_n = cn;
}



/* Live touch count, for the frame-spike reporter in main.c (tells us whether a hitching
 * frame had a finger down -- the whole question with tap-correlated stutter). */
int nx_hook_touch_count(void) { return g_hook_count; }
