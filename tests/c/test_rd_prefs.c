/* sources: source/rd_prefs.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rd_prefs.h"

static void test_string_default_when_missing(void) {
  CHECK(strcmp(rd_pref_string(NULL, "{not_found}"), "{not_found}") == 0);
  CHECK(rd_pref_string(NULL, NULL) == NULL);
}

static void test_string_stored_wins_even_if_empty(void) {
  CHECK(strcmp(rd_pref_string("abc", "def"), "abc") == 0);
  CHECK(strcmp(rd_pref_string("", "def"), "") == 0);
}

static void test_float_roundtrip_of_putfloat_format(void) {
  /* unity_jni.c putFloat stores "%.9g" */
  CHECK(rd_pref_float("0.75", 1.0f) == 0.75f);
  CHECK(rd_pref_float("-2", 1.0f) == -2.0f);
  CHECK(rd_pref_float("1e-05", 0.0f) == 1e-05f);
}

static void test_float_default_when_missing_or_garbage(void) {
  CHECK(rd_pref_float(NULL, 0.5f) == 0.5f);
  CHECK(rd_pref_float("", 0.5f) == 0.5f);
  CHECK(rd_pref_float("abc", 0.5f) == 0.5f);
  CHECK(rd_pref_float("1.5x", 0.5f) == 0.5f);
}

static char g_dir[256];

static void path_in_dir(char *out, size_t n, const char *name) {
  snprintf(out, n, "%s/%s", g_dir, name);
}

static void test_file_roundtrip_long_and_special_values(void) {
  char path[300]; path_in_dir(path, sizeof path, "rt.kv");
  size_t big_n = 100000;                         /* far beyond any fixed line buffer */
  char *big = malloc(big_n + 1);
  for (size_t i = 0; i < big_n; i++) big[i] = (char)('A' + i % 26);
  big[big_n] = 0;
  FILE *f = fopen(path, "wb");
  rd_prefs_write_entry(f, 's', "TrollpantsDevString", big);
  rd_prefs_write_entry(f, 'i', "tab\tkey", "a\\b\nc");
  rd_prefs_write_entry(f, 's', "empty", "");
  fclose(f);

  f = fopen(path, "rb");
  char *line = NULL, *k, *v, t; size_t cap = 0;
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 1);
  CHECK(t == 's' && !strcmp(k, "TrollpantsDevString") && strlen(v) == big_n && !strcmp(v, big));
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 1);
  CHECK(t == 'i' && !strcmp(k, "tab\tkey") && !strcmp(v, "a\\b\nc"));
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 1);
  CHECK(!strcmp(k, "empty") && !strcmp(v, ""));
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 0);
  fclose(f); free(line); free(big);
}

static void test_skips_malformed_lines_and_reads_last_line_without_newline(void) {
  char path[300]; path_in_dir(path, sizeof path, "bad.kv");
  FILE *f = fopen(path, "wb");
  fputs("\ngarbage\nx\tnokey\ns\tgood\tyes", f);
  fclose(f);
  f = fopen(path, "rb");
  char *line = NULL, *k, *v, t; size_t cap = 0;
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 1 && !strcmp(k, "good") && !strcmp(v, "yes"));
  CHECK(rd_prefs_read_entry(f, &line, &cap, &t, &k, &v) == 0);
  fclose(f); free(line);
}

static void test_commit_replaces_and_pick_recovers(void) {
  char path[300], tmp[300];
  path_in_dir(path, sizeof path, "p.kv"); path_in_dir(tmp, sizeof tmp, "p.kv.tmp");
  remove(path); remove(tmp);
  CHECK(rd_prefs_pick(path, tmp) == NULL);
  FILE *f = fopen(tmp, "wb"); fputs("new", f); fclose(f);
  CHECK(rd_prefs_pick(path, tmp) == tmp);         /* interrupted save: use the tmp file */
  f = fopen(path, "wb"); fputs("old", f); fclose(f);
  CHECK(rd_prefs_commit(tmp, path) == 0);
  char buf[8] = {0};
  f = fopen(path, "rb"); CHECK(fread(buf, 1, sizeof buf - 1, f) == 3); fclose(f);
  CHECK(!strcmp(buf, "new"));
  CHECK(rd_prefs_pick(path, tmp) == path);
  CHECK(rd_prefs_commit(tmp, path) == -1);         /* nothing to commit */
}

int main(int argc, char **argv) {
  snprintf(g_dir, sizeof g_dir, "%s", argc > 1 ? argv[1] : ".");
  RUN(test_file_roundtrip_long_and_special_values);
  RUN(test_skips_malformed_lines_and_reads_last_line_without_newline);
  RUN(test_commit_replaces_and_pick_recovers);
  RUN(test_string_default_when_missing);
  RUN(test_string_stored_wins_even_if_empty);
  RUN(test_float_roundtrip_of_putfloat_format);
  RUN(test_float_default_when_missing_or_garbage);
  return g_failures != 0;
}
