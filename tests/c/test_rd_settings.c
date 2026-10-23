/* sources: source/game/rd_settings.c source/platform/rd_perf.c */
/* Host unit test of rd_settings. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_settings.h"

/* Button lists accept any case, commas, plus signs and spaces, NONE and empty unbind, and an
 * unknown or overlong name is an error that leaves the binding untouched. */
static void test_parse_buttons(void) {
  uint64_t m = 0;
  CHECK(rd_parse_buttons("ZR", &m) == 0 && m == RD_BTN_ZR);
  CHECK(rd_parse_buttons(" zr, a+B  ", &m) == 0 && m == (RD_BTN_ZR | RD_BTN_A | RD_BTN_B));
  CHECK(rd_parse_buttons("NONE", &m) == 0 && m == 0);
  CHECK(rd_parse_buttons("", &m) == 0 && m == 0);
  m = 7;
  CHECK(rd_parse_buttons("ZR,FOO", &m) == -1 && m == 7);      /* unknown name: untouched */
  CHECK(rd_parse_buttons("AAAAAAAAAAAAAAAAAAAA", &m) == -1);  /* too long */
}

/* Formatted button lists parse back to the same mask, an empty mask is written as NONE, and a
 * buffer too small is an error rather than a cut-off name. */
static void test_format_roundtrip(void) {
  char buf[64]; uint64_t m;
  CHECK(rd_format_buttons(RD_BTN_ZR | RD_BTN_A, buf, sizeof buf) == 0);
  CHECK(strcmp(buf, "A,ZR") == 0);
  CHECK(rd_parse_buttons(buf, &m) == 0 && m == (RD_BTN_ZR | RD_BTN_A));
  CHECK(rd_format_buttons(0, buf, sizeof buf) == 0 && strcmp(buf, "NONE") == 0);
  CHECK(rd_format_buttons(RD_BTN_ZR | RD_BTN_A, buf, 3) == -1);
}

/* The defaults match the README: driving on the triggers, gears on the D-pad, pause on +, and
 * both smoke knobs at 100 so the game looks original, menus on the controller with A / B. */
static void test_defaults_put_gears_on_dpad(void) {
  rd_settings s = RD_SETTINGS_DEFAULT;
  const rd_bindings b = s.binds;
  CHECK(b.action[RD_ACT_GEAR_UP] == RD_BTN_UP);
  CHECK(b.action[RD_ACT_GEAR_DOWN] == RD_BTN_DOWN);
  CHECK(b.action[RD_ACT_THROTTLE] == RD_BTN_ZR && b.action[RD_ACT_HANDBRAKE] == RD_BTN_B);
  CHECK(b.steer_left == RD_BTN_LEFT && b.steer_right == RD_BTN_RIGHT);
  CHECK(b.deadzone == RD_DEADZONE_DEFAULT);
  CHECK(s.pause == RD_BTN_PLUS && s.hide_touch_controls == 1);
  CHECK(s.render_res == RD_RES_AUTO && s.boost_loading == 1);
  CHECK(s.smoke_size == RD_SMOKE_PERCENT_ORIG && s.smoke_amount == RD_SMOKE_PERCENT_ORIG);
  CHECK(s.debug_log == RD_LOG_MINIMAL && s.debug_fps_counter == 0);
  CHECK(s.menu_navigation == 1 && s.ui_select == RD_BTN_A && s.ui_back == RD_BTN_B);
  CHECK(s.ui_prev == RD_BTN_L && s.ui_next == RD_BTN_R);
  CHECK(s.debug_ui_nav == 0 && s.debug_mem == 0 && s.texture_transcode == 1
        && s.archive_cache == 1 && s.fast_boot == 1);
  CHECK(s.debug_load == 0 && s.debug_audio == 0);
}

/* index of `key` in config.txt order */
static int idx(const char *key) {
  for (int i = 0; i < rd_settings_count(); i++)
    if (!strcmp(rd_settings_key(i), key)) return i;
  return -99;
}

/* Each key validates its own type and range and keeps the old value on a bad one; removed
 * options (gc_mode, language) are reported as unknown so the file gets rewritten. */
static void test_settings_set(void) {
  rd_settings s = RD_SETTINGS_DEFAULT;
  CHECK(rd_settings_set(&s, "bind_handbrake", "A,B") == idx("bind_handbrake"));
  CHECK(s.binds.action[RD_ACT_HANDBRAKE] == (RD_BTN_A | RD_BTN_B));
  CHECK(rd_settings_set(&s, "bind_gear_up", "R") >= 0 && s.binds.action[RD_ACT_GEAR_UP] == RD_BTN_R);
  CHECK(rd_settings_set(&s, "bind_steer_left", "NONE") >= 0 && s.binds.steer_left == 0);
  CHECK(rd_settings_set(&s, "stick_deadzone", "0.3") == idx("stick_deadzone") && s.binds.deadzone == 0.3f);
  CHECK(rd_settings_set(&s, "stick_deadzone", "2") == -1 && s.binds.deadzone == 0.3f);
  CHECK(rd_settings_set(&s, "stick_deadzone", "x") == -1);
  CHECK(s.binds.action[RD_ACT_CAMERA] == 0 && s.binds.clutch == RD_BTN_Y);   /* defaults */
  CHECK(rd_settings_set(&s, "bind_camera", "Q") == -1 && s.binds.action[RD_ACT_CAMERA] == 0);
  CHECK(rd_settings_set(&s, "bind_clutch", "X") == idx("bind_clutch") && s.binds.clutch == RD_BTN_X);
  CHECK(rd_settings_set(&s, "bind_pause", "MINUS") >= 0 && s.pause == RD_BTN_MINUS);
  CHECK(rd_settings_set(&s, "hide_touch_controls", "0") >= 0 && s.hide_touch_controls == 0);
  CHECK(rd_settings_set(&s, "hide_touch_controls", " 1") >= 0 && s.hide_touch_controls == 1);
  CHECK(rd_settings_set(&s, "hide_touch_controls", "yes") == -1 && s.hide_touch_controls == 1);
  CHECK(rd_settings_set(&s, "hide_touch_controls", "10") == -1);
  CHECK(rd_settings_set(&s, "gc_mode", "off") == -2);           /* removed option */
  CHECK(rd_settings_set(&s, "render_resolution", "1080") >= 0 && s.render_res == RD_RES_1080);
  CHECK(rd_settings_set(&s, "render_resolution", " AUTO") >= 0 && s.render_res == RD_RES_AUTO);
  CHECK(rd_settings_set(&s, "render_resolution", "900") == -1 && s.render_res == RD_RES_AUTO);
  CHECK(rd_settings_set(&s, "boost_loading", "0") >= 0 && s.boost_loading == 0);
  CHECK(rd_settings_set(&s, "boost_loading", "2") == -1 && s.boost_loading == 0);
  CHECK(rd_settings_set(&s, "debug_log", "0") >= 0 && s.debug_log == 0);
  CHECK(rd_settings_set(&s, "debug_log", "full") == -1 && s.debug_log == 0);
  CHECK(rd_settings_set(&s, "language", "fr") == -2);           /* removed option */
  CHECK(rd_settings_set(&s, "debug_fps_counter", "1") == idx("debug_fps_counter") && s.debug_fps_counter == 1);
  CHECK(rd_settings_set(&s, "debug_fps_counter", "on") == -1 && s.debug_fps_counter == 1);
  CHECK(rd_settings_set(&s, "smoke_size", "40") == idx("smoke_size") && s.smoke_size == 40);
  CHECK(rd_settings_set(&s, "smoke_size", "0.5") == -1 && s.smoke_size == 40);
  CHECK(rd_settings_set(&s, "smoke_size", "9") == -1);
  CHECK(rd_settings_set(&s, "smoke_amount", "50") == idx("smoke_amount") && s.smoke_amount == 50);
  CHECK(rd_settings_set(&s, "smoke_amount", "5") == -1 && s.smoke_amount == 50);
  CHECK(rd_settings_set(&s, "smoke_amount", "101") == -1);
  CHECK(rd_settings_set(&s, "smoke_amount", "50%") == -1);
  CHECK(rd_settings_set(&s, "menu_navigation", "0") == idx("menu_navigation") && !s.menu_navigation);
  CHECK(rd_settings_set(&s, "bind_ui_back", "B,Y") == idx("bind_ui_back") &&
        s.ui_back == (RD_BTN_B | RD_BTN_Y));
  CHECK(rd_settings_set(&s, "bind_ui_next", "ZR") == idx("bind_ui_next") && s.ui_next == RD_BTN_ZR);
  CHECK(rd_settings_count() == 31 && idx("bind_throttle") == 0);
}

/* config.txt written with non-default values reads back identically, and a file with missing
 * or bad keys asks for a rewrite while keeping defaults for them. */
static void test_write_then_read_roundtrip(const char *dir) {
  char path[256]; snprintf(path, sizeof path, "%s/settings.txt", dir);
  rd_settings in = RD_SETTINGS_DEFAULT;
  in.binds.action[RD_ACT_THROTTLE] = RD_BTN_ZR | RD_BTN_A;
  in.binds.steer_right = 0;
  in.binds.deadzone = 0.25f;
  in.binds.clutch = RD_BTN_L | RD_BTN_X;
  in.pause = RD_BTN_MINUS | RD_BTN_X;
  in.hide_touch_controls = 0;
  in.render_res = RD_RES_720;
  in.boost_loading = 0;
  in.debug_log = RD_LOG_FULL;
  in.debug_fps_counter = 1;
  in.smoke_size = 30;
  in.smoke_amount = 40;
  in.menu_navigation = 0;
  in.ui_select = RD_BTN_X;
  in.debug_ui_nav = 1;
  in.debug_mem = 1;
  in.texture_transcode = 0;
  in.debug_load = 1;
  in.debug_audio = 1;
  FILE *f = fopen(path, "w"); rd_settings_write(&in, f); fclose(f);

  rd_settings out = RD_SETTINGS_DEFAULT;
  f = fopen(path, "r");
  CHECK(rd_settings_read(&out, f) == 0);             /* every key present and valid */
  fclose(f);
  CHECK(memcmp(&in, &out, sizeof in) == 0);

  f = fopen(path, "w"); fputs("debug_log=0\nbind_camera=WHAT\n", f); fclose(f);
  rd_settings part = RD_SETTINGS_DEFAULT;
  f = fopen(path, "r");
  CHECK(rd_settings_read(&part, f) == 1);            /* missing / bad keys -> rewrite */
  fclose(f);
  CHECK(part.debug_log == 0 && part.binds.action[RD_ACT_CAMERA] == 0);
}

int main(int argc, char **argv) {
  RUN(test_parse_buttons);
  RUN(test_format_roundtrip);
  RUN(test_defaults_put_gears_on_dpad);
  RUN(test_settings_set);
  test_write_then_read_roundtrip(argc > 1 ? argv[1] : ".");
  return g_failures != 0;
}
