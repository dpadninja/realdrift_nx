/* sources: source/mods/rd_mod_ini.c */
/* mod_ini_check -- reads a mod folder's mod.ini and patch.ini with the port's own parser
 * (rd_mod_ini.c) and prints what the port would read, so tools/mod_check.py applies exactly
 * the rules the console applies. Built on the fly by tools/hostc.py.
 *
 *   mod_ini_check <mod folder>
 *
 * Output, one line each:  info <key>=<value> | patch <line> <section> <field> <value>
 *                         | error <message>
 * Exit 0 if mod.ini is valid (patch.ini problems are reported but do not fail).
 * MIT (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_mod_ini.h"

#define MAX_TEXT (64 * 1024)

static rd_ini g_ini;
static char g_text[MAX_TEXT + 1];

/* Reads path into g_text; 0, or -1 if missing or larger than the port accepts. */
static int slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  const size_t n = fread(g_text, 1, MAX_TEXT, f);
  const int big = n == MAX_TEXT && fgetc(f) != EOF;
  fclose(f);
  g_text[n] = 0;
  return big ? -1 : 0;
}

/* The last path component of dir (the mod's folder name). */
static const char *folder_of(const char *dir) {
  size_t n = strlen(dir);
  while (n > 1 && dir[n - 1] == '/') n--;
  static char buf[256];
  size_t s = n;
  while (s > 0 && dir[s - 1] != '/') s--;
  snprintf(buf, sizeof buf, "%.*s", (int)(n - s), dir + s);
  return buf;
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: mod_ini_check <mod folder>\n"); return 2; }
  char path[1024];
  snprintf(path, sizeof path, "%s/mod.ini", argv[1]);
  if (slurp(path)) { printf("error mod.ini: missing or larger than %d bytes\n", MAX_TEXT); return 1; }
  int bad = rd_ini_parse(g_text, &g_ini);
  if (bad) { printf("error mod.ini line %d is not \"key = value\"\n", bad); return 1; }
  rd_mod_info info;
  char err[200];
  if (rd_mod_info_read(&g_ini, folder_of(argv[1]), &info, err, sizeof err)) {
    printf("error %s\n", err);
    return 1;
  }
  printf("info id=%s\ninfo name=%s\ninfo version=%s\ninfo api=%d\ninfo enabled=%d\n"
         "info priority=%d\n", info.id, info.name, info.version, info.api, info.enabled,
         info.priority);
  snprintf(path, sizeof path, "%s/patch.ini", argv[1]);
  if (slurp(path) == 0) {
    bad = rd_ini_parse(g_text, &g_ini);
    if (bad) {
      printf("error patch.ini line %d is not \"key = value\"\n", bad);
    } else {
      for (int i = 0; i < g_ini.n; i++) {
        const rd_ini_entry *e = &g_ini.e[i];
        char as[RD_INI_SECTION_LEN], ns[RD_INI_SECTION_LEN], cls[RD_INI_SECTION_LEN];
        if (!e->section[0] || rd_patch_section(e->section, as, sizeof as, ns, sizeof ns, cls, sizeof cls))
          printf("error patch.ini line %d: \"%s\" is not under an [Assembly:Class] section\n", e->line, e->key);
        else
          printf("patch %d %s %s%s%s %s %s\n", e->line, as, ns, ns[0] ? "." : "", cls, e->key, e->value);
      }
    }
  }
  return 0;
}
