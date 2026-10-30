/* rd_settings.c -- see rd_settings.h. MIT (see LICENSE). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "rd_settings.h"
#include "rd_smoke.h"

static const struct { const char *name; uint64_t bit; } BUTTONS[] = {
  { "A", RD_BTN_A }, { "B", RD_BTN_B }, { "X", RD_BTN_X }, { "Y", RD_BTN_Y },
  { "L", RD_BTN_L }, { "R", RD_BTN_R }, { "ZL", RD_BTN_ZL }, { "ZR", RD_BTN_ZR },
  { "PLUS", RD_BTN_PLUS }, { "MINUS", RD_BTN_MINUS },
  { "UP", RD_BTN_UP }, { "DOWN", RD_BTN_DOWN }, { "LEFT", RD_BTN_LEFT }, { "RIGHT", RD_BTN_RIGHT },
  { "LSTICK", RD_BTN_LSTICK }, { "RSTICK", RD_BTN_RSTICK },
};
#define NBUTTONS (sizeof BUTTONS / sizeof BUTTONS[0])

typedef enum {
  T_BUTTONS,     /* uint64_t button mask */
  T_SWITCH,      /* int, 0 or 1 */
  T_DEADZONE,    /* float, 0 .. RD_DEADZONE_MAX */
  T_SMOKE,       /* int percent, RD_SMOKE_PERCENT_MIN .. RD_SMOKE_PERCENT_ORIG */
  T_RESOLUTION,  /* int, auto | 720 | 1080 */
  T_COUNTRY,     /* char[RD_COUNTRY_SIZE]: "" or two letters (ISO 3166-1 alpha-2) */
  T_DISTANCE,    /* int metres, RD_DRAW_DISTANCE_GAME .. RD_DRAW_DISTANCE_MAX */
  T_AA,          /* int samples, rd_render_aa_valid */
} type;

/* Range of a numeric type; 1 = float, 2 = int, 0 = not numeric. */
static int range(type t, float *lo, float *hi) {
  switch (t) {
    case T_DEADZONE: *lo = 0.0f; *hi = RD_DEADZONE_MAX; return 1;
    case T_SMOKE:    *lo = RD_SMOKE_PERCENT_MIN; *hi = RD_SMOKE_PERCENT_ORIG; return 2;
    case T_DISTANCE: *lo = RD_DRAW_DISTANCE_GAME; *hi = RD_DRAW_DISTANCE_MAX; return 2;
    default:             return 0;
  }
}

typedef struct {
  const char *key;
  type        type;
  size_t      offset, size;   /* the field in rd_settings */
  const char *help;           /* comment lines above the key ("" = none); for a numeric type
                                 a printf format taking its range (double / int, see range()) */
} setting;

#define F(field) offsetof(rd_settings, field), sizeof(((rd_settings *)0)->field)

/* The order here is the order in config.txt. */
static const setting SETTINGS[] = {
  { "bind_throttle", T_BUTTONS, F(binds.action[RD_ACT_THROTTLE]),
    "\n# Driving controls (only while a car is driven; menus use touch / the cursor).\n"
    "# One or more of: A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT LSTICK RSTICK,\n"
    "# separated by commas; NONE to unbind. The left stick always steers.\n" },
  { "bind_brake",       T_BUTTONS, F(binds.action[RD_ACT_BRAKE]),     "" },
  { "bind_handbrake",   T_BUTTONS, F(binds.action[RD_ACT_HANDBRAKE]), "" },
  { "bind_clutch",      T_BUTTONS, F(binds.clutch),                   "" },
  { "bind_gear_up",     T_BUTTONS, F(binds.action[RD_ACT_GEAR_UP]),   "" },
  { "bind_gear_down",   T_BUTTONS, F(binds.action[RD_ACT_GEAR_DOWN]), "" },
  { "bind_camera",      T_BUTTONS, F(binds.action[RD_ACT_CAMERA]),    "" },
  { "bind_steer_left",  T_BUTTONS, F(binds.steer_left),               "" },
  { "bind_steer_right", T_BUTTONS, F(binds.steer_right),              "" },
  { "stick_deadzone",   T_DEADZONE, F(binds.deadzone),
    "# left stick deadzone, %.1f .. %.1f\n" },
  { "bind_pause",       T_BUTTONS, F(pause),
    "# pause / resume the race (also closes the pause menu)\n" },
  { "hide_touch_controls", T_SWITCH, F(hide_touch_controls),
    "# 1 hides the on-screen steering/throttle/brake/handbrake/gear buttons in races\n" },
  { "menu_navigation", T_SWITCH, F(menu_navigation),
    "\n# Menus.\n"
    "# menu_navigation: 1 = a frame marks a menu element; the D-pad or the left stick moves it,\n"
    "#   bind_ui_select taps it, bind_ui_back goes back. The touchscreen keeps working.\n"
    "#   0 = the on-screen cursor of earlier versions (stick moves it, A taps, + hides it).\n" },
  { "bind_ui_select",   T_BUTTONS, F(ui_select), "" },
  { "bind_ui_back",     T_BUTTONS, F(ui_back),   "" },
  { "bind_ui_prev",     T_BUTTONS, F(ui_prev),
    "# bind_ui_prev / bind_ui_next: previous / next car in the garage, tab, leaderboard track.\n"
    "#   bind_ui_select and bind_ui_back also skip the track preview before a race.\n" },
  { "bind_ui_next",     T_BUTTONS, F(ui_next),   "" },

  { "offline_ui", T_SWITCH, F(offline_ui),
    "\n# Offline.\n"
    "# offline_ui: 1 hides what needs the internet: the coin shop (+), Google sign-in, cloud\n"
    "#   save / load, online leaderboards (the button reads STATISTICS and shows this\n"
    "#   console's records), Share and links to web pages, and stops Unity Analytics from\n"
    "#   queueing its events on the SD card. 0 = as in the game.\n" },
  { "country", T_COUNTRY, F(country),
    "# country: the flag (two letters, e.g. JP, DE, US) used until one is picked in the\n"
    "#   profile; empty = the game's own choice (Andorra, as its web lookup fails offline).\n" },

  { "render_resolution", T_RESOLUTION, F(render_res),
    "\n# Performance.\n"
    "# render_resolution: auto = 720 handheld / 1080 docked (picked at launch), or 720 / 1080.\n"
    "#   Handheld shows 720p anyway; rendering 1080p there costs 2.25x pixels (smoke!).\n" },
  { "boost_loading", T_SWITCH, F(boost_loading),
    "# boost_loading: 1 raises the CPU clock (" RD_BOOST_CPU_MHZ " MHz) while a scene is loading.\n" },
  { "smoke_size", T_SMOKE, F(smoke_size),
    "# Heavy tyre smoke is what drops the frame rate (the GPU, not the CPU). Both keys at\n"
    "# 100 = the original game.\n"
    "# smoke_size: size of the smoke puffs in percent, %d .. %d (try 50).\n" },
  { "smoke_amount", T_SMOKE, F(smoke_amount),
    "# smoke_amount: percent of the smoke puffs that are emitted, %d .. %d (try 50).\n" },
  { "draw_distance", T_DISTANCE, F(draw_distance),
    "# draw_distance (experimental): metres the camera sees at least, %d .. %d; 0 = as\n"
    "#   the game. Only the two mountain tracks (360 m) are shorter than 1000 m. Their trees\n"
    "#   are flat cards (cheap); beyond ~1000 m the edge of the mountain model may show.\n" },
  { "antialiasing", T_AA, F(antialiasing),
    "# antialiasing (experimental): MSAA samples, 0 (off), 2, 4 or 8. Smooths the edges of\n"
    "#   the car, the road and the rails, not of foliage. Costs GPU time.\n" },
  { "texture_transcode", T_SWITCH, F(texture_transcode),
    "# texture_transcode: 1 stores the game's ETC2 textures as S3TC, which the GPU reads\n"
    "#   directly; 0 leaves them to mesa, which unpacks each one to 4-8x the memory, so after\n"
    "#   many cars and tracks new textures stop fitting and show black. Loads take a bit longer.\n" },
  { "archive_cache", T_SWITCH, F(archive_cache),
    "# archive_cache: 1 keeps recently read parts of base.apk and the OBB in memory (64 MB),\n"
    "#   shared by every open of them, so loads read each part from the SD card once; 0 reads\n"
    "#   the card directly (slower loads).\n" },
  { "fast_boot", T_SWITCH, F(fast_boot),
    "# fast_boot: 1 starts faster -- Unity's il2cpp data is copied from the APK to the il2cpp\n"
    "#   folder only when the APK changed (not on every launch), and the blank 2 s Unity splash\n"
    "#   screen is skipped; 0 = as the original game.\n" },
  { "debug_log", T_SWITCH, F(debug_log),
    "# debug_log: 1 = full debug.log (send it when reporting problems), 0 = only crashes,\n"
    "#   stalls, GC and startup lines. The log is buffered either way.\n" },
  { "debug_fps_counter", T_SWITCH, F(debug_fps_counter),
    "# debug_fps_counter: 1 shows frames per second and the slowest frame (ms) of the last\n"
    "#   half second in the top-left corner.\n" },
  { "debug_gl_trace", T_SWITCH, F(debug_gl_trace),
    "# debug_gl_trace: 1 logs where a frame's GPU time goes, per render target ([gltrace]\n"
    "#   lines, with debug_log = 1), about every 10 s. Frames run slower while one is taken.\n" },
  { "debug_ui_nav", T_SWITCH, F(debug_ui_nav),
    "# debug_ui_nav: 1 logs the elements menu navigation finds on each new screen ([uinav]\n"
    "#   lines, with debug_log = 1): names, parents, rectangles.\n" },
  { "debug_mem", T_SWITCH, F(debug_mem),
    "# debug_mem: 1 logs where the memory is about every 10 s and after each load, and every\n"
    "#   GL error when a texture or buffer is created ([mem] lines, with debug_log = 1).\n" },
  { "debug_load", T_SWITCH, F(debug_load),
    "# debug_load: 1 logs, for every frame of a second or more (a load), the file traffic the\n"
    "#   engine asked for and what reached the SD card, and each engine thread's CPU time\n"
    "#   ([load] lines, with debug_log = 1).\n" },
  { "debug_audio", T_SWITCH, F(debug_audio),
    "# debug_audio: 1 logs when the sound goes silent for 3 s or comes back, when the audio\n"
    "#   callback stops, and the engine's volume / mute calls ([audio] lines, with debug_log = 1).\n" },
};
#define NSETTINGS ((int)(sizeof SETTINGS / sizeof SETTINGS[0]))

int rd_settings_count(void) { return NSETTINGS; }
const char *rd_settings_key(int i) { return i >= 0 && i < NSETTINGS ? SETTINGS[i].key : NULL; }

int rd_parse_buttons(const char *s, uint64_t *out) {
  uint64_t mask = 0;
  char tok[16];
  while (s && *s) {
    while (*s == ',' || *s == '+' || isspace((unsigned char)*s)) s++;
    if (!*s) break;
    size_t n = 0;
    while (*s && *s != ',' && *s != '+' && !isspace((unsigned char)*s)) {
      if (n + 1 >= sizeof tok) return -1;
      tok[n++] = *s++;
    }
    tok[n] = '\0';
    if (!strcasecmp(tok, "NONE")) continue;
    size_t i = 0;
    while (i < NBUTTONS && strcasecmp(tok, BUTTONS[i].name)) i++;
    if (i == NBUTTONS) return -1;
    mask |= BUTTONS[i].bit;
  }
  *out = mask;
  return 0;
}

int rd_format_buttons(uint64_t mask, char *buf, size_t n) {
  if (!buf || !n) return -1;
  buf[0] = '\0';
  size_t len = 0;
  for (size_t i = 0; i < NBUTTONS; i++) {
    if (!(mask & BUTTONS[i].bit)) continue;
    int w = snprintf(buf + len, n - len, "%s%s", len ? "," : "", BUTTONS[i].name);
    if (w < 0 || (size_t)w >= n - len) return -1;
    len += (size_t)w;
  }
  if (!len) {
    if (n < 5) return -1;
    strcpy(buf, "NONE");
  }
  return 0;
}

/* value without leading/trailing whitespace, in buf */
static const char *trim(const char *v, char *buf, size_t n) {
  while (isspace((unsigned char)*v)) v++;
  size_t len = strlen(v);
  while (len && isspace((unsigned char)v[len - 1])) len--;
  if (len >= n) len = n - 1;
  memcpy(buf, v, len);
  buf[len] = '\0';
  return buf;
}

/* Parses `value` for setting st into the rd_settings field; 0, or -1 if invalid (the field is
 * left unchanged). */
static int parse(const setting *st, const char *value, void *field) {
  char v[128];
  trim(value, v, sizeof v);
  switch (st->type) {
    case T_BUTTONS: {
      uint64_t mask;
      if (rd_parse_buttons(v, &mask) != 0) return -1;
      *(uint64_t *)field = mask;
      return 0;
    }
    case T_SWITCH:
      if ((v[0] != '0' && v[0] != '1') || v[1]) return -1;
      *(int *)field = v[0] == '1';
      return 0;
    case T_DEADZONE: {
      float lo, hi;
      range(st->type, &lo, &hi);
      char *end;
      float x = strtof(v, &end);
      if (end == v || *end || x < lo || x > hi) return -1;
      *(float *)field = x;
      return 0;
    }
    case T_SMOKE:
    case T_DISTANCE: {
      float lo, hi;
      range(st->type, &lo, &hi);
      char *end;
      long x = strtol(v, &end, 10);
      if (end == v || *end || x < (long)lo || x > (long)hi) return -1;
      *(int *)field = (int)x;
      return 0;
    }
    case T_AA: {
      char *end;
      long x = strtol(v, &end, 10);
      if (end == v || *end || !rd_render_aa_valid((int)x)) return -1;
      *(int *)field = (int)x;
      return 0;
    }
    case T_RESOLUTION:
      if (!strcasecmp(v, "auto"))  *(int *)field = RD_RES_AUTO;
      else if (!strcmp(v, "720"))  *(int *)field = RD_RES_720;
      else if (!strcmp(v, "1080")) *(int *)field = RD_RES_1080;
      else return -1;
      return 0;
    case T_COUNTRY: {
      char *out = field;
      size_t len = strlen(v);
      if (len && len != RD_COUNTRY_SIZE - 1) return -1;
      for (size_t i = 0; i < len; i++) {
        if (!isalpha((unsigned char)v[i])) return -1;
        out[i] = (char)toupper((unsigned char)v[i]);
      }
      out[len] = '\0';
      return 0;
    }
  }
  return -1;
}

/* The value of setting st as it is written to config.txt. */
static void format(const setting *st, const void *field, char *buf, size_t n) {
  switch (st->type) {
    case T_BUTTONS:  if (rd_format_buttons(*(const uint64_t *)field, buf, n)) snprintf(buf, n, "NONE"); break;
    case T_SWITCH:   snprintf(buf, n, "%d", *(const int *)field ? 1 : 0); break;
    case T_DEADZONE: snprintf(buf, n, "%.2f", (double)*(const float *)field); break;
    case T_SMOKE:
    case T_DISTANCE:
    case T_AA:       snprintf(buf, n, "%d", *(const int *)field); break;
    case T_RESOLUTION: {
      int r = *(const int *)field;
      snprintf(buf, n, "%s", r == RD_RES_720 ? "720" : r == RD_RES_1080 ? "1080" : "auto");
      break;
    }
    case T_COUNTRY:  snprintf(buf, n, "%s", (const char *)field); break;
  }
}

int rd_settings_set(rd_settings *s, const char *key, const char *value) {
  for (int i = 0; i < NSETTINGS; i++) {
    if (strcmp(key, SETTINGS[i].key)) continue;
    return parse(&SETTINGS[i], value ? value : "", (char *)s + SETTINGS[i].offset) == 0 ? i : -1;
  }
  return -2;
}

int rd_settings_read(rd_settings *s, FILE *f) {
  unsigned char seen[NSETTINGS] = {0};
  char line[256];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') continue;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    char key[48];
    snprintf(key, sizeof key, "%.*s", (int)(eq - line), line);
    eq[1 + strcspn(eq + 1, "\r\n")] = '\0';
    int i = rd_settings_set(s, key, eq + 1);
    if (i >= 0) seen[i] = 1;     /* a bad value keeps the default and is "not seen" */
  }
  for (int i = 0; i < NSETTINGS; i++)
    if (!seen[i]) return 1;
  return 0;
}

void rd_settings_write(const rd_settings *s, FILE *f) {
  fprintf(f, "# Real Drift (realdrift_nx) config\n");
  for (int i = 0; i < NSETTINGS; i++) {
    char buf[128];
    format(&SETTINGS[i], (const char *)s + SETTINGS[i].offset, buf, sizeof buf);
    float lo, hi;
    switch (range(SETTINGS[i].type, &lo, &hi)) {
      case 1:  fprintf(f, SETTINGS[i].help, (double)lo, (double)hi); break;
      case 2:  fprintf(f, SETTINGS[i].help, (int)lo, (int)hi); break;
      default: fputs(SETTINGS[i].help, f);
    }
    fprintf(f, "%s=%s\n", SETTINGS[i].key, buf);
  }
}
