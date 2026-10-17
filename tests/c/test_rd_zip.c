/* sources: source/android/rd_zip.c */
/* Host unit test of rd_zip. MIT (see LICENSE). */
/* libs: -lz */
/* Reads the zip that tests/test_c_units.py writes next to the binary (argv[1]/test.zip):
 *   stored.txt   (stored)    "hello stored"
 *   deflated.txt (deflated)  "hello deflated " * 100
 *   dir/boot.config (deflated) "gfx-enable-gfx-jobs=\n" */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rd_zip.h"

static FILE *g_f;

/* A stored (uncompressed) APK entry is found and read whole. */
static void test_stored(void) {
  rd_zip_entry e;
  CHECK(rd_zip_find(g_f, "stored.txt", &e) == 0 && e.method == 0 && e.size == 12);
  char *s = rd_zip_read(g_f, &e);
  CHECK(s && strcmp(s, "hello stored") == 0);
  free(s);
}

/* Deflated entries, also in subdirectories (Real Drift's APK deflates almost everything under
 * assets/), inflate to their full size. */
static void test_deflated(void) {
  rd_zip_entry e;
  CHECK(rd_zip_find(g_f, "deflated.txt", &e) == 0 && e.method == 8 && e.size == 1500);
  CHECK(e.comp_size < e.size);
  char *s = rd_zip_read(g_f, &e);
  CHECK(s && strlen(s) == 1500 && strncmp(s, "hello deflated hello", 20) == 0);
  free(s);
  CHECK(rd_zip_find(g_f, "dir/boot.config", &e) == 0);
  s = rd_zip_read(g_f, &e);
  CHECK(s && strcmp(s, "gfx-enable-gfx-jobs=\n") == 0);
  free(s);
}

/* A missing name, a name that is only a prefix of an entry, NULL, an unsupported method and a
 * file that is not a zip all fail cleanly. */
static void test_missing_and_bad_input(void) {
  rd_zip_entry e;
  CHECK(rd_zip_find(g_f, "nope.txt", &e) == -1);
  CHECK(rd_zip_find(g_f, "dir/boot", &e) == -1);          /* prefix is not a match */
  CHECK(rd_zip_find(NULL, "stored.txt", &e) == -1);
  rd_zip_entry bad = { 0, 1, 1, 12 };                      /* unsupported method */
  CHECK(rd_zip_read(g_f, &bad) == NULL);
  FILE *notzip = tmpfile(); fputs("not a zip at all, definitely not", notzip);
  CHECK(rd_zip_find(notzip, "stored.txt", &e) == -1);
  fclose(notzip);
}

int main(int argc, char **argv) {
  char path[512];
  snprintf(path, sizeof path, "%s/test.zip", argc > 1 ? argv[1] : ".");
  g_f = fopen(path, "rb");
  CHECK(g_f != NULL);
  if (!g_f) return 1;
  RUN(test_stored);
  RUN(test_deflated);
  RUN(test_missing_and_bad_input);
  fclose(g_f);
  return g_failures != 0;
}
