/* sources: source/config.c source/rd_settings.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "config.h"

static char g_path[256];

static void write_file(const char *text) {
  FILE *f = fopen(g_path, "w");
  fputs(text, f);
  fclose(f);
}

static void reset(void) {
  rd_settings def = RD_SETTINGS_DEFAULT;
  config = def;
}

static void test_missing_file(void) {
  reset();
  unlink(g_path);
  CHECK(read_config(g_path) == -1);
  CHECK(config.debug_log == RD_LOG_MINIMAL);
}

static void test_reads_a_key_and_strips_cr(void) {
  reset();
  write_file("# comment\n\ndebug_log=1\r\n");
  CHECK(read_config(g_path) == 1);            /* other keys missing -> rewrite */
  CHECK(config.debug_log == RD_LOG_FULL);
}

static void test_stale_keys_request_rewrite(void) {
  reset();
  write_file("# only a comment\ncheats=1\nlanguage=fr\n");   /* removed keys */
  CHECK(read_config(g_path) == 1);
  CHECK(config.debug_log == RD_LOG_MINIMAL);
}

static void test_bindings_roundtrip_and_custom_values(void) {
  reset();
  config.binds.action[RD_ACT_HANDBRAKE] = RD_BTN_A | RD_BTN_B;
  config.binds.deadzone = 0.3f;
  config.debug_log = RD_LOG_FULL;
  CHECK(write_config(g_path) == 0);
  reset();
  CHECK(read_config(g_path) == 0);
  CHECK(config.binds.action[RD_ACT_HANDBRAKE] == (RD_BTN_A | RD_BTN_B));
  CHECK(config.binds.action[RD_ACT_GEAR_UP] == RD_BTN_UP);
  CHECK(config.binds.deadzone == 0.3f);
  CHECK(config.debug_log == RD_LOG_FULL);
}

static void test_bad_binding_keeps_default_and_requests_rewrite(void) {
  reset();
  CHECK(write_config(g_path) == 0);
  FILE *f = fopen(g_path, "a"); fputs("bind_camera=WHAT\r\n", f); fclose(f);
  /* the valid bind_camera line from write_config came first; the bad one is ignored */
  CHECK(read_config(g_path) == 0);
  CHECK(config.binds.action[RD_ACT_CAMERA] == RD_BTN_Y);
  write_file("debug_log=0\nbind_camera=WHAT\n");
  CHECK(read_config(g_path) == 1);
  CHECK(config.binds.action[RD_ACT_CAMERA] == RD_BTN_Y);
}

int main(int argc, char **argv) {
  snprintf(g_path, sizeof g_path, "%s/config.txt", argc > 1 ? argv[1] : ".");
  RUN(test_missing_file);
  RUN(test_reads_a_key_and_strips_cr);
  RUN(test_stale_keys_request_rewrite);
  RUN(test_bindings_roundtrip_and_custom_values);
  RUN(test_bad_binding_keeps_default_and_requests_rewrite);
  return g_failures != 0;
}
