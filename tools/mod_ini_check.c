/* sources: source/mods/rd_mod_ini.c source/mods/rd_mod_cars.c */
/* mod_ini_check -- reads a mod folder's mod.ini and patch.ini with the port's own parser
 * (rd_mod_ini.c) and prints what the port would read, so tools/mod_check.py applies exactly
 * the rules the console applies. Built on the fly by tools/hostc.py.
 *
 *   mod_ini_check <mod folder>
 *   mod_ini_check --keys
 *
 * Output, one line each:  info <key>=<value> | error <message>
 *   | patch<TAB>line<TAB>assembly<TAB>class<TAB>filter<TAB>field<TAB>value (tabs: names and
 *     values may contain spaces; filter is empty for every object)
 *   | car<TAB>line<TAB>car<TAB>class (empty: a car component)<TAB>field<TAB>value<TAB>key
 *     (key: the cars.ini key of rd_car_props, empty for a field)
 *   | carkey<TAB>key<TAB>unit<TAB>kind<TAB>when<TAB>class<TAB>field<TAB>axle<TAB>names
 *     (with --keys: the table of cars.ini keys, for tools and docs; kind "field", "axle" or
 *     "special", when "live", "once" or "build", axle "front", "rear", "both" or "", names
 *     the enum's value names separated by spaces or "")
 * Exit 0 if mod.ini is valid (patch.ini problems are reported but do not fail).
 * MIT (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_mod_cars.h"
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
  if (argc == 2 && !strcmp(argv[1], "--keys")) {
    int n;
    const rd_car_prop *props = rd_car_props(&n);
    static const char *const WHEN[] = { "live", "once", "build" };
    static const char *const AXLE[] = { "", "front", "rear", "both" };
    for (int i = 0; i < n; i++) {
      const rd_car_prop *k = &props[i];
      printf("carkey\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t", k->key, k->unit,
             k->kind == RD_CAR_FIELD ? "field" : k->kind == RD_CAR_AXLE ? "axle" : "special",
             WHEN[k->when], k->cls, k->field, AXLE[k->axle & 3]);
      for (int j = 0; k->names && k->names[j]; j++) printf("%s%s", j ? " " : "", k->names[j]);
      printf("\n");
    }
    return 0;
  }
  if (argc != 2) { fprintf(stderr, "usage: mod_ini_check <mod folder> | --keys\n"); return 2; }
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
        rd_patch_target t;
        if (!e->section[0] || rd_patch_target_parse(e->section, &t))
          printf("error patch.ini line %d: \"%s\" is not under an [Assembly:Class @ filter] section\n",
                 e->line, e->key);
        else
          printf("patch\t%d\t%s\t%s%s%s\t%s\t%s\t%s\n", e->line, t.assembly, t.ns, t.ns[0] ? "." : "",
                 t.cls, t.filter, e->key, e->value);
      }
    }
  }
  snprintf(path, sizeof path, "%s/cars.ini", argv[1]);
  if (slurp(path) == 0) {
    static rd_car_value cars[RD_INI_MAX_ENTRIES];
    bad = rd_ini_parse(g_text, &g_ini);
    const int n = bad ? -1 : rd_cars_read(&g_ini, cars, RD_INI_MAX_ENTRIES, err, sizeof err);
    if (bad) printf("error cars.ini line %d is not \"key = value\"\n", bad);
    else if (n < 0) printf("error %s\n", err);
    for (int i = 0; i < n; i++)
      printf("car\t%d\t%s\t%s\t%s\t%s\t%s\n", cars[i].line, cars[i].car, cars[i].cls,
             cars[i].field, cars[i].value, cars[i].prop ? cars[i].prop->key : "");
  }
  return 0;
}
