/* rd_settings.h -- the port's settings, stored in config.txt (RD_CONFIG_FILE).
 * Host-testable (tests/c/test_rd_settings.c); config.example.txt is generated from it.
 *
 * Every key is one row of the table in rd_settings.c (name, type, field, help text), which
 * drives parsing, writing and the comments in config.txt.
 *
 * Button values are one or more button names separated by ',' '+' or spaces (any of them
 * triggers the action); "NONE" or an empty value unbinds. Names: A B X Y L R ZL ZR PLUS MINUS
 * UP DOWN LEFT RIGHT LSTICK RSTICK (case-insensitive). Bit values follow libnx
 * HidNpadButton_* (checked at compile time in rd_drive_hook.c). */
#ifndef RD_SETTINGS_H
#define RD_SETTINGS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "rd_drive.h"
#include "rd_perf.h"
#include "rd_log.h"
#include "rd_smoke.h"

#define RD_BTN_A      (1ull << 0)
#define RD_BTN_B      (1ull << 1)
#define RD_BTN_X      (1ull << 2)
#define RD_BTN_Y      (1ull << 3)
#define RD_BTN_LSTICK (1ull << 4)
#define RD_BTN_RSTICK (1ull << 5)
#define RD_BTN_L      (1ull << 6)
#define RD_BTN_R      (1ull << 7)
#define RD_BTN_ZL     (1ull << 8)
#define RD_BTN_ZR     (1ull << 9)
#define RD_BTN_PLUS   (1ull << 10)
#define RD_BTN_MINUS  (1ull << 11)
#define RD_BTN_LEFT   (1ull << 12)
#define RD_BTN_UP     (1ull << 13)
#define RD_BTN_RIGHT  (1ull << 14)
#define RD_BTN_DOWN   (1ull << 15)

#define RD_DEADZONE_DEFAULT  0.15f
#define RD_DEADZONE_MAX      0.9f

typedef struct {
  rd_bindings binds;                 /* driving: buttons per action, steering, deadzone */
  uint64_t    pause;                 /* toggles the race pause menu (rd_race_hook.c) */
  int         hide_touch_controls;   /* 1: hide on-screen driving buttons during a race */
  int         render_res;            /* RD_RES_AUTO / RD_RES_720 / RD_RES_1080 (rd_perf.h) */
  int         boost_loading;         /* 1: CPU boost while a loading frame runs (rd_perf.h) */
  int         smoke_size;            /* percent of the smoke puffs' size (rd_smoke.h) */
  int         smoke_amount;          /* percent of the smoke puffs emitted (rd_smoke.h) */
  int         debug_log;             /* RD_LOG_FULL / RD_LOG_MINIMAL (rd_log.h) */
  int         debug_fps_counter;     /* 1: FPS overlay in the top-left corner (rd_fps.h) */
} rd_settings;

#define RD_SETTINGS_DEFAULT {                                         \
  .binds = {                                                          \
    .action = {                                                       \
      [RD_ACT_THROTTLE]  = RD_BTN_ZR,   [RD_ACT_BRAKE]     = RD_BTN_ZL, \
      [RD_ACT_HANDBRAKE] = RD_BTN_B,    [RD_ACT_GEAR_UP]   = RD_BTN_UP, \
      [RD_ACT_GEAR_DOWN] = RD_BTN_DOWN, [RD_ACT_CAMERA]    = RD_BTN_Y,  \
    },                                                                \
    .steer_left = RD_BTN_LEFT, .steer_right = RD_BTN_RIGHT,           \
    .deadzone = RD_DEADZONE_DEFAULT,                                  \
  },                                                                  \
  .pause = RD_BTN_PLUS,                                               \
  .hide_touch_controls = 1,                                           \
  .render_res = RD_RES_AUTO,                                          \
  .boost_loading = 1,                                                 \
  .smoke_size = RD_SMOKE_PERCENT_ORIG,                                \
  .smoke_amount = RD_SMOKE_PERCENT_ORIG,                              \
  .debug_log = RD_LOG_MINIMAL,                                        \
  .debug_fps_counter = 0,                                             \
}

/* Parses a button list. Returns 0 and *out on success, -1 on an unknown name. */
int rd_parse_buttons(const char *s, uint64_t *out);

/* "ZR,A" style text for `mask` ("NONE" if 0). Returns 0, or -1 if it did not fit. */
int rd_format_buttons(uint64_t mask, char *buf, size_t n);

/* Number of keys in config.txt. */
int rd_settings_count(void);
/* Name of key i (0 .. count-1), in the order written to config.txt. */
const char *rd_settings_key(int i);

/* Applies one key. Returns the key's index if `value` is valid, -1 if the key is known but
 * the value is invalid (the setting is left unchanged), -2 for an unknown key. */
int rd_settings_set(rd_settings *s, const char *key, const char *value);

/* Reads key=value lines (# comments, blank lines and unknown keys are skipped).
 * Returns 0 if every key was present with a valid value, 1 otherwise (the caller then
 * rewrites the file, so it self-heals to the current schema). */
int rd_settings_read(rd_settings *s, FILE *f);

/* Writes every key with its help text. */
void rd_settings_write(const rd_settings *s, FILE *f);

#endif
