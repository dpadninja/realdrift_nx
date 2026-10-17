/* config.c -- config.txt (RD_CONFIG_FILE) load/save. The keys, defaults and help texts are
 * the table in rd_settings.c. MIT (see LICENSE).
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE). */
#include <stdio.h>
#include "config.h"

int screen_width  = 0;
int screen_height = 0;

Config config = RD_SETTINGS_DEFAULT;

/* Returns 0 = read clean, -1 = missing, 1 = present but keys missing/invalid
 * (caller rewrites so the file self-heals to the current schema). */
int read_config(const char *file) {
  FILE *f = fopen(file, "r");
  if (!f) return -1;
  int r = rd_settings_read(&config, f);
  fclose(f);
  return r;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (!f) return -1;
  rd_settings_write(&config, f);
  fclose(f);
  return 0;
}
