/* nx_pointer.c -- see nx_pointer.h.
 *
 * Everything pointer-related for a Switch port of an Android game: touchscreen, USB mouse,
 * gyro and stick cursor, and the settings file. The cursor is drawn by nx_pointer_draw.c.
 *
 * Origin: badpiggies_nx by ChanseyIsTheBest, https://github.com/ChanseyIsTheBest/badpiggies_nx
 * (commit 7726be3); the same module ships in cloverpit_nx by ChanseyIsTheBest,
 * https://github.com/ChanseyIsTheBest/cloverpit_nx. The stick cursor and its GL overlay were
 * ported there from laytonbmr_nx by naga, https://github.com/NaGaa95/laytonbmr_nx.
 *
 * Copyright (c) 2026 cloverpit_nx contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified for realdrift_nx: the GL drawing moved to rd_overlay_gl.c, the cursor to
 * nx_pointer_draw.c.
 */
#include <switch.h>
#include <stdarg.h>   /* va_list / va_start / va_end in nxp_logf() */
#include <string.h>
#include <math.h>

#include "nx_pointer.h"
#include "nx_pointer_internal.h"
#include "config.h"
#include "rd_race.h"    /* rd_race_active */
#include "rd_platform.h"

/* ------------------------------------------------------------------ config */

NxpConfig nxp_cfg;
int       nxp_ready = 0;

void nxp_logf(const char *fmt, ...) {
  if (!nxp_cfg.log) return;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  nxp_cfg.log(buf);
}

/* ------------------------------------------------------------------- state */

static PadState s_pad;

/* touch (handheld) */
static int   s_touch_active[16];
static float s_touch_x[16], s_touch_y[16];

/* cursor */
float nxp_cx, nxp_cy;
int   nxp_visible   = -1;      /* -1 = not yet decided */
static int   s_was_docked = -1;
static int   s_tap_prev  = 0;       /* A or mouse-left held last frame */

/* tunables, adjusted live */
/* Cursor travel multiplier while a tap is held (see the drag-boost comment further down).
 * 1.0 = no boost (a drag moves at pointing speed). Raise for flickier swipes, lower for finer
 * dragging. Applies to the virtual cursor only -- never to the physical touchscreen. */
#define NXP_DRAG_BOOST 2.5f

static float s_stick_speed;         /* px/frame at full stick deflection */
static float s_mouse_sens;          /* multiplier on mouse deltas */
static float s_gyro_sens;           /* multiplier on gyro angular velocity */

/* gyro (motion pointing). Handles differ per controller style, so we fetch one
 * for each and pick at read time based on what is actually connected. */
static HidSixAxisSensorHandle s_six[4];
static int s_gyro_ready = 0;        /* sensors started */
static int s_gyro_on    = 0;        /* toggled by '-' */
static int s_gyro_logged = 0;

/* A USB mouse takes priority: gyro is switched off while one is connected. */
static int s_mouse_connected = 0;

/* Raw angular velocity -> px/frame. The gyro reports a RATE, so this is the
 * per-frame gain; s_gyro_sens scales it and D-pad U/D tunes that live.
 * Measured on hardware: a normal turn gives |angular_velocity| ~0.14, so at the
 * original gain of 40 that was only ~6 px/frame -- far too slow. Tripled. */
#define GYRO_GAIN 120.0f

/* d-pad auto-repeat for the sensitivity adjustment */
static int   s_dpad_hold = 0;

/* Settings persistence. A save is queued whenever a sensitivity changes and
 * committed 3s after the LAST change, so holding the D-pad through twenty steps
 * writes the file once rather than twenty times. */
#define SETTINGS_DEBOUNCE_NS  3000000000ULL      /* 3 seconds */
static int s_settings_dirty = 0;
static u64 s_settings_tick  = 0;

/* events for this frame */
static NxpEvent s_ev[24];
static int      s_nev;

#define STICK_MIN   2.0f
#define STICK_MAX  60.0f
#define SENS_MIN    0.25f
#define SENS_MAX    8.0f
#define GYRO_MIN    0.10f
#define GYRO_MAX    8.0f

static int is_docked(void) {
  return appletGetOperationMode() == AppletOperationMode_Console;
}

/* ------------------------------------------------------- settings file ---
 * IMPORTANT: use the port's locked fopen/fclose when it supplies them. The
 * engine's worker threads are doing file I/O constantly, and devkitPro's newlib
 * handle table is not thread-safe -- an unlocked open/close from here would race
 * it and corrupt the fd table (which shows up as a Data Abort deep inside
 * newlib, nowhere near this code). */
FILE *nxp_fopen(const char *path, const char *mode) {
  return nxp_cfg.fopen_fn ? nxp_cfg.fopen_fn(path, mode) : fopen(path, mode);
}
int nxp_fclose(FILE *f) {
  return nxp_cfg.fclose_fn ? nxp_cfg.fclose_fn(f) : fclose(f);
}

static void clamp_settings(void) {
  if (s_stick_speed < STICK_MIN) s_stick_speed = STICK_MIN;
  if (s_stick_speed > STICK_MAX) s_stick_speed = STICK_MAX;
  if (s_mouse_sens  < SENS_MIN)  s_mouse_sens  = SENS_MIN;
  if (s_mouse_sens  > SENS_MAX)  s_mouse_sens  = SENS_MAX;
  if (s_gyro_sens   < GYRO_MIN)  s_gyro_sens   = GYRO_MIN;
  if (s_gyro_sens   > GYRO_MAX)  s_gyro_sens   = GYRO_MAX;
}

static void settings_path(char *out, size_t n) {
  snprintf(out, n, "%s/pointer.cfg", nxp_cfg.data_dir ? nxp_cfg.data_dir : ".");
}

static void settings_load(void) {
  if (!nxp_cfg.data_dir) return;
  char path[512];
  settings_path(path, sizeof path);

  FILE *f = nxp_fopen(path, "r");
  if (!f) { nxp_logf("nxp: no pointer.cfg -- using defaults\n"); return; }

  char line[128];
  while (fgets(line, sizeof line, f)) {
    float v;
    if      (sscanf(line, "stick=%f", &v) == 1) s_stick_speed = v;
    else if (sscanf(line, "mouse=%f", &v) == 1) s_mouse_sens  = v;
    else if (sscanf(line, "gyro=%f",  &v) == 1) s_gyro_sens   = v;
  }
  nxp_fclose(f);
  clamp_settings();                      /* the file may have been hand-edited */
  nxp_logf("nxp: settings loaded  stick=%.1f mouse=%.2f gyro=%.2f\n",
        s_stick_speed, s_mouse_sens, s_gyro_sens);
}

void nxp_save_settings(void) {
  if (!nxp_ready || !nxp_cfg.data_dir) return;
  char path[512];
  settings_path(path, sizeof path);

  FILE *f = nxp_fopen(path, "w");
  if (!f) { nxp_logf("nxp: could not write %s\n", path); s_settings_dirty = 0; return; }

  fprintf(f, "# nx_pointer settings -- auto-saved, safe to edit\n");
  fprintf(f, "stick=%.2f\n", s_stick_speed);
  fprintf(f, "mouse=%.2f\n", s_mouse_sens);
  fprintf(f, "gyro=%.2f\n",  s_gyro_sens);
  nxp_fclose(f);

  s_settings_dirty = 0;
  nxp_logf("nxp: settings saved  stick=%.1f mouse=%.2f gyro=%.2f\n",
        s_stick_speed, s_mouse_sens, s_gyro_sens);
}

/* Queue a save; it lands SETTINGS_DEBOUNCE_NS after the last change. */
static void settings_touch(void) {
  s_settings_dirty = 1;
  s_settings_tick  = armGetSystemTick();
}

/* Called once per frame from nxp_update(). */
static void settings_tick(void) {
  if (!s_settings_dirty) return;
  if (armTicksToNs(armGetSystemTick() - s_settings_tick) >= SETTINGS_DEBOUNCE_NS)
    nxp_save_settings();
}

/* ------------------------------------------------------------------- init */

void nxp_init(const NxpConfig *cfg) {
  if (nxp_ready) return;
  memset(&nxp_cfg, 0, sizeof nxp_cfg);
  if (cfg) nxp_cfg = *cfg;

  if (nxp_cfg.panel_w <= 0)         nxp_cfg.panel_w = RD_TOUCH_PANEL_W;
  if (nxp_cfg.panel_h <= 0)         nxp_cfg.panel_h = RD_TOUCH_PANEL_H;
  if (nxp_cfg.cursor_id <= 0)       nxp_cfg.cursor_id = 8;
  if (nxp_cfg.max_touch_slots <= 0) nxp_cfg.max_touch_slots = 8;
  if (nxp_cfg.screen_w <= 0)        nxp_cfg.screen_w = 1920;
  if (nxp_cfg.screen_h <= 0)        nxp_cfg.screen_h = 1080;

  s_stick_speed = (nxp_cfg.stick_speed > 0.0f) ? nxp_cfg.stick_speed : 14.0f;
  s_mouse_sens  = (nxp_cfg.mouse_sens  > 0.0f) ? nxp_cfg.mouse_sens  : 1.0f;
  s_gyro_sens   = 1.0f;

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&s_pad);
  hidInitializeTouchScreen();
  hidInitializeMouse();               /* USB / dock mouse */

  /* Six-axis (gyro): each controller style has its own handle, so grab them all
   * and choose the right one per frame from the active style. */
  Result rc0 = hidGetSixAxisSensorHandles(&s_six[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  Result rc1 = hidGetSixAxisSensorHandles(&s_six[1], 1, HidNpadIdType_No1,      HidNpadStyleTag_NpadFullKey);
  Result rc2 = hidGetSixAxisSensorHandles(&s_six[2], 2, HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyDual);
  if (R_SUCCEEDED(rc0) && R_SUCCEEDED(rc1) && R_SUCCEEDED(rc2)) {
    for (int i = 0; i < 4; i++) hidStartSixAxisSensor(s_six[i]);
    s_gyro_ready = 1;
  } else {
    nxp_logf("nxp: gyro unavailable (sensor handles failed)\n");
  }

  nxp_cx = nxp_cfg.screen_w * 0.5f;
  nxp_cy = nxp_cfg.screen_h * 0.5f;

  settings_load();                    /* restores stick/mouse/gyro sensitivities */
  nxp_slurp_cursor_png();                 /* read now, decode on the render thread */

  nxp_ready = 1;
  nxp_logf("nxp: init %dx%d (panel %dx%d), stick=%.1f mouse=%.2f\n",
        nxp_cfg.screen_w, nxp_cfg.screen_h, nxp_cfg.panel_w, nxp_cfg.panel_h,
        s_stick_speed, s_mouse_sens);
}

/* ---------------------------------------------------------------- helpers */

static void clamp_cursor(void) {
  if (nxp_cx < 0) nxp_cx = 0;
  if (nxp_cy < 0) nxp_cy = 0;
  if (nxp_cx > nxp_cfg.screen_w - 1) nxp_cx = (float)(nxp_cfg.screen_w - 1);
  if (nxp_cy > nxp_cfg.screen_h - 1) nxp_cy = (float)(nxp_cfg.screen_h - 1);
}

static void push(int id, float x, float y, int phase) {
  if (s_nev >= (int)(sizeof s_ev / sizeof s_ev[0])) return;
  NxpEvent *e = &s_ev[s_nev++];
  e->id = id; e->x = x; e->y = y; e->phase = phase;
}

/* Touch: the panel always reports in its own space (1280x720) regardless of the
 * resolution we render at, so scale into render space -- otherwise the right and
 * bottom edges (and the corners) are physically unreachable. */
static void do_touch(void) {
  HidTouchScreenState ts = {0};
  hidGetTouchScreenStates(&ts, 1);

  const int slots = nxp_cfg.max_touch_slots;
  int now[16] = {0};
  int count = ts.count > slots ? slots : ts.count;

  const float sx = (float)nxp_cfg.screen_w / (float)nxp_cfg.panel_w;
  const float sy = (float)nxp_cfg.screen_h / (float)nxp_cfg.panel_h;

  for (int i = 0; i < count; i++) {
    float x = (float)ts.touches[i].x * sx;
    float y = (float)ts.touches[i].y * sy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > nxp_cfg.screen_w - 1) x = (float)(nxp_cfg.screen_w - 1);
    if (y > nxp_cfg.screen_h - 1) y = (float)(nxp_cfg.screen_h - 1);
    now[i] = 1;
    push(i, x, y, s_touch_active[i] ? NXP_MOVE : NXP_DOWN);
    s_touch_x[i] = x; s_touch_y[i] = y;
  }
  for (int i = 0; i < slots; i++) {
    if (s_touch_active[i] && !now[i])
      push(i, s_touch_x[i], s_touch_y[i], NXP_UP);
    s_touch_active[i] = now[i];
  }
}

/* D-pad UP/DOWN adjusts sensitivity. That is the ONLY sensitivity control --
 * left/right used to also tune the stick, which just meant two buttons doing
 * overlapping jobs and no clear answer to "which one am I changing?".
 *
 * Up/Down applies to whichever device is ACTUALLY driving the cursor right now:
 *   a mouse is plugged in  -> mouse sensitivity
 *   else gyro is on        -> gyro sensitivity
 *   else                   -> stick speed
 * so it is never a no-op, and there is only ever one thing it can mean.
 * Auto-repeats while held. Any change queues a debounced save. */
static void do_dpad(u64 held, u64 pressed) {
  const u64 any = HidNpadButton_Up | HidNpadButton_Down;

  int repeat = 0;
  if (held & any) {
    s_dpad_hold++;
    if (s_dpad_hold > 24 && (s_dpad_hold % 3) == 0) repeat = 1;   /* ~0.4s, then repeat */
  } else {
    s_dpad_hold = 0;
  }

  int step = 0;
  if (pressed & HidNpadButton_Up)   step = +1;
  if (pressed & HidNpadButton_Down) step = -1;
  if (repeat && (held & HidNpadButton_Up))   step = +1;
  if (repeat && (held & HidNpadButton_Down)) step = -1;
  if (!step) return;

  const float f = (step > 0) ? 1.15f : (1.0f / 1.15f);

  if (s_mouse_connected) {
    s_mouse_sens *= f;
    clamp_settings();
    nxp_logf("nxp: mouse sensitivity = %.2f\n", s_mouse_sens);
  } else if (s_gyro_on) {
    s_gyro_sens *= f;
    clamp_settings();
    nxp_logf("nxp: gyro sensitivity = %.2f\n", s_gyro_sens);
  } else {
    s_stick_speed *= f;
    clamp_settings();
    nxp_logf("nxp: stick speed = %.1f px/frame\n", s_stick_speed);
  }

  settings_touch();                 /* save 3s after the last change */
}

/* Pick the six-axis handle that matches whatever is actually connected, then
 * read its latest sample. Handheld, Pro Controller and dual Joy-Con all report
 * through different handles. */
static int read_gyro(HidSixAxisSensorState *out, float *sign_x, float *sign_y) {
  if (!s_gyro_ready) return 0;
  const u64 style = padGetStyleSet(&s_pad);

  /* AXIS SIGNS.
   * The Pro Controller's IMU frame gives the correct pointing direction with
   * both axes negated. A Joy-Con's IMU sits rotated 180 degrees about its
   * pointing axis relative to that, so BOTH axes come out mirrored -- which is
   * exactly what shows up in practice (Pro correct, Joy-Con reversed on both).
   * Keep the signs in one place so a single controller can be re-flipped
   * without touching the rest of the maths. */
  *sign_x = -1.0f;
  *sign_y = -1.0f;                                  /* Pro Controller */

  if (style & HidNpadStyleTag_NpadFullKey)
    return hidGetSixAxisSensorStates(s_six[1], out, 1) > 0;

  if (style & HidNpadStyleTag_NpadHandheld) {
    *sign_x = +1.0f; *sign_y = +1.0f;               /* Joy-Con frame: mirrored */
    return hidGetSixAxisSensorStates(s_six[0], out, 1) > 0;
  }

  if (style & HidNpadStyleTag_NpadJoyDual) {
    *sign_x = +1.0f; *sign_y = +1.0f;               /* Joy-Con frame: mirrored */
    const u64 attr = padGetAttributes(&s_pad);
    if (attr & HidNpadAttribute_IsRightConnected)
      return hidGetSixAxisSensorStates(s_six[3], out, 1) > 0;
    if (attr & HidNpadAttribute_IsLeftConnected)
      return hidGetSixAxisSensorStates(s_six[2], out, 1) > 0;
  }
  return 0;
}

/* Motion pointing: tilt/turn the controller to move the cursor.
 *
 * The gyro reports an angular VELOCITY, so we treat it as a per-frame rate:
 *   yaw   (rotation about the controller's up axis)    -> cursor X
 *   pitch (rotation about the controller's right axis) -> cursor Y
 * Signs are negated so the cursor follows where you point the controller.
 * A connected mouse wins, so gyro is skipped entirely while one is plugged in. */
static void do_gyro(void) {
  if (!s_gyro_on || s_mouse_connected) return;

  HidSixAxisSensorState st = {0};
  float sx = -1.0f, sy = -1.0f;
  if (!read_gyro(&st, &sx, &sy)) return;

  if (s_gyro_logged < 5) {
    s_gyro_logged++;
    nxp_logf("nxp: gyro av=(%.3f, %.3f, %.3f) signs=(%+.0f,%+.0f)\n",
          st.angular_velocity.x, st.angular_velocity.y, st.angular_velocity.z, sx, sy);
  }

  const float g = GYRO_GAIN * s_gyro_sens;
  const float dx = sx * st.angular_velocity.y * g;   /* yaw   -> horizontal */
  const float dy = sy * st.angular_velocity.x * g;   /* pitch -> vertical   */

  /* small deadzone so a resting controller does not creep */
  if (fabsf(dx) > 0.05f || fabsf(dy) > 0.05f) {
    nxp_cx += dx;
    nxp_cy += dy;
    clamp_cursor();
  }
}

/* USB mouse: relative motion drives the cursor; the wheel tunes sensitivity.
 *
 * IMPORTANT: read the whole sample buffer, not just the newest state. HID samples
 * faster than we render, and a wheel tick shows up in exactly ONE sample -- if we
 * only look at the latest one each frame, most ticks land in samples we never see
 * and the wheel appears dead. Accumulate every sample we have not processed yet
 * (tracked by sampling_number). */
static u64 s_mouse_seen = 0;
static int s_mouse_logged = 0;

static int do_mouse(void) {
  HidMouseState st[16];
  int n = (int)hidGetMouseStates(st, 16);
  if (n <= 0) return 0;

  s32 dx = 0, dy = 0, wheel_y = 0, wheel_x = 0;
  u32 buttons = 0;
  u64 newest = s_mouse_seen, btn_sn = 0;

  for (int i = 0; i < n; i++) {
    const HidMouseState *m = &st[i];
    if (m->sampling_number > btn_sn) { btn_sn = m->sampling_number; buttons = m->buttons; }
    if (m->sampling_number <= s_mouse_seen) continue;      /* already handled */
    dx      += m->delta_x;
    dy      += m->delta_y;
    wheel_y += m->wheel_delta_y;
    wheel_x += m->wheel_delta_x;      /* some drivers put vertical scroll here */
    if (m->sampling_number > newest) newest = m->sampling_number;
  }

  /* Use whichever axis actually reports. On this hardware wheel_delta_y stayed 0
   * while the mouse otherwise worked perfectly, so don't assume either one. */
  s32 wheel = wheel_y ? wheel_y : wheel_x;

  /* Presence: trust the IsConnected attribute, but treat any real activity as
   * proof of a mouse too (in case a device never sets the flag). */
  u32 attrs = 0;
  for (int i = 0; i < n; i++)
    if (st[i].sampling_number == btn_sn) { attrs = st[i].attributes; break; }
  int conn = (attrs & HidMouseAttribute_IsConnected) ? 1 : 0;
  if (!conn && (dx || dy || wheel_x || wheel_y || buttons)) conn = 1;

  if (conn != s_mouse_connected) {
    s_mouse_connected = conn;
    nxp_logf("nxp: mouse %s\n", conn ? "connected" : "disconnected");
    if (conn && s_gyro_on) {
      s_gyro_on = 0;                 /* a mouse takes priority over motion */
      nxp_logf("nxp: gyro OFF (mouse connected)\n");
    }
  }

  if (s_mouse_seen == 0) {           /* first sight of the mouse: sync, don't jump */
    s_mouse_seen = newest;
    nxp_logf("nxp: mouse detected (%d samples buffered)\n", n);
    return (buttons & HidMouseButton_Left) ? 1 : 0;
  }
  s_mouse_seen = newest;

  /* Log every event that carries a wheel value, plus a few movement samples, so
   * a dead wheel is immediately visible in the log rather than a mystery. */
  if ((wheel_x || wheel_y) || s_mouse_logged < 3) {
    if (s_mouse_logged < 40) {
      s_mouse_logged++;
      nxp_logf("nxp: mouse dx=%d dy=%d wheel_y=%d wheel_x=%d buttons=0x%x\n",
            (int)dx, (int)dy, (int)wheel_y, (int)wheel_x, (unsigned)buttons);
    }
  }

  if (wheel != 0) {
    /* one notch = one step; several notches in a frame compound */
    for (int i = 0; i < (wheel > 0 ? wheel : -wheel) && i < 8; i++)
      s_mouse_sens *= (wheel > 0) ? 1.15f : (1.0f / 1.15f);
    if (s_mouse_sens < SENS_MIN) s_mouse_sens = SENS_MIN;
    if (s_mouse_sens > SENS_MAX) s_mouse_sens = SENS_MAX;
    nxp_logf("nxp: mouse sensitivity = %.2f\n", s_mouse_sens);
  }

  if (dx || dy) {
    nxp_cx += (float)dx * s_mouse_sens;
    nxp_cy += (float)dy * s_mouse_sens;
    clamp_cursor();
    if (nxp_visible <= 0) nxp_visible = 1;     /* moving the mouse brings it up */
  }

  return (buttons & HidMouseButton_Left) ? 1 : 0;
}

/* ----------------------------------------------------------------- update */

void nxp_update(void) {
  if (!nxp_ready) return;
  padUpdate(&s_pad);

  const int docked = is_docked();
  if (docked != s_was_docked) {
    /* docked has no touchscreen, so make sure there is always a way to point */
    if (nxp_visible < 0 || docked) nxp_visible = docked ? 1 : 0;
    s_was_docked = docked;
  }

  const u64 held    = padGetButtons(&s_pad);
  const u64 pressed = padGetButtonsDown(&s_pad);

  /* '+' toggles the cursor; '-' toggles gyro pointing.
   * in a race, '+' does not toggle the cursor if it is bound to pause. */
  const int plus_is_pause = rd_race_active() && (config.pause & HidNpadButton_Plus);
  if ((pressed & HidNpadButton_Plus) && !plus_is_pause) {
    nxp_visible = (nxp_visible > 0) ? 0 : 1;
    nxp_logf("nxp: cursor %s\n", nxp_visible ? "ON" : "OFF");
  }
  if (pressed & HidNpadButton_Minus) {
    if (s_mouse_connected) {
      nxp_logf("nxp: gyro stays OFF while a mouse is connected\n");
    } else if (!s_gyro_ready) {
      nxp_logf("nxp: gyro unavailable on this controller\n");
    } else {
      s_gyro_on = !s_gyro_on;
      nxp_logf("nxp: gyro %s\n", s_gyro_on ? "ON" : "OFF");
      if (s_gyro_on && nxp_visible <= 0) nxp_visible = 1;   /* pointing needs a cursor */
    }
  }

  /* L / R recenter the cursor to the middle of the screen. Handy with gyro:
   * point the controller where it's comfortable, recenter, and aim from there
   * (like recentering a VR view). Works in every mode. */
  /* While a car is being driven the controller belongs to rd_drive_hook.c:
   * no cursor movement, recentering, sensitivity steps or ZR/ZL/A taps. Touch still works. */
  const int driving = rd_drive_active();

  if (!driving && (pressed & (HidNpadButton_L | HidNpadButton_R))) {
    nxp_cx = nxp_cfg.screen_w * 0.5f;
    nxp_cy = nxp_cfg.screen_h * 0.5f;
    if (nxp_visible <= 0) nxp_visible = 1;    /* show it so you can see where it went */
    nxp_logf("nxp: cursor recentered\n");
  }

  if (!driving) do_dpad(held, pressed);

  s_nev = 0;

  if (!docked) do_touch();                 /* touchscreen: handheld only */

  /* Mouse first: it sets s_mouse_connected, which gates the gyro below. */
  const int mouse_tap = do_mouse();
  do_gyro();                               /* no-op if off, or if a mouse is in */

  if (nxp_visible > 0 && !driving) {
    HidAnalogStickState ls = padGetStickPos(&s_pad, 0);
    if (ls.x || ls.y) {
      /* DRAG BOOST. While a tap is held the cursor is dragging, not pointing, so it needs to
       * cover ground: games recognise a swipe/flick by how far the contact travels per frame,
       * and the pointing speed (14 px/frame ~ 840 px/s) is too slow to clear that bar on a
       * 1920-wide screen -- the drag reads as a slow press instead of a swipe. Boosting only
       * while held keeps pointing precise for aiming and part placement.
       *
       * This affects the VIRTUAL CURSOR only. The physical touchscreen is fed straight through
       * in do_touch() at true panel coordinates and is deliberately untouched -- a finger
       * already swipes exactly as it does on a phone. */
      const float sp = s_stick_speed * (s_tap_prev ? NXP_DRAG_BOOST : 1.0f);
      nxp_cx += (ls.x / (float)JOYSTICK_MAX) * sp;
      nxp_cy -= (ls.y / (float)JOYSTICK_MAX) * sp;   /* stick +y is up */
      clamp_cursor();
    }

    /* A, ZR and ZL all confirm/tap (ZL/ZR let you play one-handed), as does the
     * mouse's left button. */
    const int tap = ((held & (HidNpadButton_A | HidNpadButton_ZR | HidNpadButton_ZL)) ? 1 : 0)
                    | mouse_tap;
    int phase = 0;
    if      ( tap && !s_tap_prev) phase = NXP_DOWN;
    else if ( tap &&  s_tap_prev) phase = NXP_MOVE;
    else if (!tap &&  s_tap_prev) phase = NXP_UP;
    s_tap_prev = tap;
    if (phase) push(nxp_cfg.cursor_id, nxp_cx, nxp_cy, phase);
  } else {
    s_tap_prev = 0;
  }

  settings_tick();          /* commits a queued save 3s after the last change */
}

int nxp_poll(NxpEvent *out, int max) {
  int n = s_nev < max ? s_nev : max;
  if (n > 0) memcpy(out, s_ev, (size_t)n * sizeof(NxpEvent));
  return n;
}

/* Raw controller state for rd_drive_hook.c (driving bindings). */
uint64_t nxp_pad_held(void)    { return nxp_ready ? padGetButtons(&s_pad) : 0; }
float    nxp_pad_stick_x(void) { if (!nxp_ready) return 0.0f;
                                 HidAnalogStickState ls = padGetStickPos(&s_pad, 0);
                                 return ls.x / (float)JOYSTICK_MAX; }

int   nxp_cursor_visible(void)          { return nxp_visible > 0; }
void  nxp_cursor_pos(float *x, float *y){ if (x) *x = nxp_cx; if (y) *y = nxp_cy; }
float nxp_stick_speed(void)             { return s_stick_speed; }
float nxp_mouse_sens(void)              { return s_mouse_sens; }
float nxp_gyro_sens(void)               { return s_gyro_sens; }
int   nxp_gyro_enabled(void)            { return s_gyro_on && !s_mouse_connected; }
int   nxp_mouse_connected(void)         { return s_mouse_connected; }
