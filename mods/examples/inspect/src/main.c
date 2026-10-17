/* inspect -- logs the values of chosen fields of live objects (mod.ini: class, fields,
 * objects). For finding out what a value is before patching it. MIT (see LICENSE). */
#include "rd_mod.h"

#define MAX_FIELDS 8
#define NAME_LEN   48
#define EVERY      600   /* frames: about 10 s */

static const rd_mod_api *api;
static void *klass;
static int nfields, nobjects = 1;
static char names[MAX_FIELDS][NAME_LEN], types[MAX_FIELDS];
static uint32_t offsets[MAX_FIELDS];

/* Copies s up to the first stop character (or end) into out; returns where it stopped. */
static const char *copy_until(const char *s, char stop, char *out, int n) {
  int i = 0;
  while (*s && *s != stop && *s != ' ' && i < n - 1) out[i++] = *s++;
  out[i] = 0;
  return s;
}

static void dump(const char *why) {
  void *objs[8];
  int n = klass ? api->find_objects(klass, objs, 8) : 0;
  api->log(api, "%s: %d objects", why, n);
  for (int o = 0; o < n && o < nobjects; o++)
    for (int f = 0; f < nfields; f++) {
      const char *p = (const char *)objs[o] + offsets[f];
      if (types[f] == 'f') api->log(api, "  #%d %s = %f", o, names[f], (double)*(const float *)p);
      else if (types[f] == 'b') api->log(api, "  #%d %s = %d", o, names[f], *(const unsigned char *)p);
      else api->log(api, "  #%d %s = %d", o, names[f], *(const int *)p);
    }
}

static void on_ready(void *user) {
  (void)user;
  char as[NAME_LEN], ns[NAME_LEN] = "", cls[NAME_LEN], typ[8];
  const char *s = api->ini(api, "class");
  if (!s) { api->log(api, "no class = in mod.ini"); return; }
  s = copy_until(s, ':', as, NAME_LEN);
  if (*s == ':') s++;
  const char *dot = 0;
  for (const char *q = s; *q; q++) if (*q == '.') dot = q;
  if (dot) { int i = 0; while (s < dot && i < NAME_LEN - 1) ns[i++] = *s++; ns[i] = 0; s++; }
  copy_until(s, 0, cls, NAME_LEN);
  klass = api->find_class(as, ns, cls);
  if (!klass) { api->log(api, "class %s / %s / %s not found", as, ns, cls); return; }
  for (const char *f = api->ini(api, "fields"); f && *f && nfields < MAX_FIELDS;) {
    while (*f == ' ') f++;
    f = copy_until(f, ':', names[nfields], NAME_LEN);
    if (*f == ':') f = copy_until(f + 1, 0, typ, sizeof typ);
    else typ[0] = 'i';
    types[nfields] = typ[0];
    if (api->field(klass, names[nfields], &offsets[nfields]) == 0) nfields++;
    else api->log(api, "no field %s", names[nfields]);
  }
  const char *o = api->ini(api, "objects");
  if (o && *o >= '1' && *o <= '8') nobjects = *o - '0';
}

static void on_scene(void *user, const char *scene) { (void)user; dump(scene); }
static void on_frame(void *user, uint32_t frame) { (void)user; if (frame % EVERY == 0) dump("tick"); }

RD_MOD_ENTRY {
  api = m;
  *user = 0;
  m->on_game_ready(m, on_ready);
  m->on_scene(m, on_scene);
  return m->on_frame(m, on_frame);
}
