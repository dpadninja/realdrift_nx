/* rd_mods_patches.c -- patch.ini and cars.ini values written into the game's live objects.
 * Loaded at boot (rd_mods.c), resolved once the game is ready, applied after each scene change
 * and every RD_MODS_PATCH_FRAMES frames (main thread).
 *
 * A value is one patch: a class, how to write it (a field, or one of cars.ini's own keys:
 * rd_mod_cars.h) and which objects (all, a patch.ini "@ filter" of object names, or a car).
 * A car is a GameObject carrying a CarController -- the garage and menu models carry none --
 * and an object belongs to the car whose object it is under (wheels are child objects).
 * Base values (RD_CAR_BASE) go into the car's Vehicle record in PersistentData.vehicles
 * instead, which the game reads when it next builds the car.
 * MIT (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_mod_cars.h"
#include "rd_mod_patch.h"
#include "rd_mods_internal.h"
#include "util.h"

#define PATCH_INI    "patch.ini"
#define CARS_INI     "cars.ini"
#define MAX_PATCHES  256
#define MAX_OBJECTS  256          /* objects of one class patched per pass */
#define MAX_CARS     32           /* cars in one scene */

typedef struct {
  const char      *id;           /* the mod's, for log lines */
  rd_ini_entry     e;            /* patch.ini: section = class; key = field, value */
  rd_patch_target  t;            /* the class; t.filter limits patch.ini values to object names */
  char             car[RD_INI_SECTION_LEN];   /* cars.ini: the car ("*" = all), "" = patch.ini */
  const rd_car_prop *prop;       /* cars.ini: its own key, NULL for a field */
  void            *klass;
  uint32_t         offset;
  uint32_t         aux[2];       /* more field offsets of the special keys (resolve_car_key) */
  void            *method;       /* drive: SetTransmission */
  rd_patch_value   v;
  int              ok;
} patch;

static patch *g_patches;
static int    g_npatches;

/* Cars of the current pass: their transform and name. */
static void *g_car_tf[MAX_CARS];
static char  g_car_name[MAX_CARS][RD_MODS_NAME_LEN];
static int   g_ncars;

static void *(*g_transform)(void *component);
static void *(*g_parent)(void *transform);

/* A new patch of mod `id`, or NULL (logged) when the table is full. */
static patch *add_patch(const char *id) {
  if (!g_patches) g_patches = calloc(MAX_PATCHES, sizeof *g_patches);
  if (!g_patches || g_npatches == MAX_PATCHES) {
    debugPrintf(RD_LOG_TAG_MODS " %s: too many patch and car values\n", id);
    return NULL;
  }
  patch *p = &g_patches[g_npatches++];
  *p = (patch){ .id = id };
  return p;
}

int rd_mods_patches_load(const char *id, const rd_ini *patches, const rd_ini *cars) {
  const int before = g_npatches;
  for (int i = 0; patches && i < patches->n; i++) {
    if (!patches->e[i].section[0]) continue;
    patch *p = add_patch(id);
    if (!p) break;
    p->e = patches->e[i];
  }
  static rd_car_value v[MAX_PATCHES];
  char err[200];
  const int n = cars ? rd_cars_read(cars, v, MAX_PATCHES, err, sizeof err) : 0;
  if (n < 0) debugPrintf(RD_LOG_TAG_MODS " %s: %s\n", id, err);
  /* [*] first, so that a car's own section wins wherever it stands in the file */
  for (int k = 0; k < 2 * n; k++) {
    const int i = k % n;
    if ((k < n) != !strcmp(v[i].car, RD_CAR_ALL)) continue;
    patch *p = add_patch(id);
    if (!p) break;
    _Static_assert(sizeof p->car == sizeof v[i].car && sizeof p->e.key == sizeof v[i].field &&
                   sizeof p->e.value == sizeof v[i].value && sizeof p->t.cls >= sizeof v[i].cls,
                   "cars.ini value buffers");
    memcpy(p->car, v[i].car, sizeof p->car);
    memcpy(p->e.key, v[i].field, sizeof p->e.key);
    memcpy(p->e.value, v[i].value, sizeof p->e.value);
    memcpy(p->t.cls, v[i].cls, sizeof v[i].cls);
    p->e.line = v[i].line;
    p->prop = v[i].prop;
    snprintf(p->t.assembly, sizeof p->t.assembly, "%s", RD_ASM_GAME);
  }
  return g_npatches - before;
}

/* ---- resolving ---- */

/* The class of a parsed section (nested classes "Outer/Inner"), or NULL. */
static void *patch_class(rd_patch_target *t) {
  char *nested = strchr(t->cls, '/');
  if (nested) *nested++ = 0;
  void *k = rd_il2cpp_class(&g_il2cpp, t->assembly, t->ns, t->cls);
  while (k && nested) {
    char *next = strchr(nested, '/');
    if (next) *next++ = 0;
    k = rd_il2cpp_nested_class(&g_il2cpp, k, nested);
    nested = next;
  }
  return k;
}

/* For a cars.ini field without "Class.": the one car component (RD_CAR_COMPONENTS) that has
 * it. Logs and returns NULL when none or several have it. */
static void *car_component(patch *p) {
  static const char *const COMPONENTS[] = RD_CAR_COMPONENTS;
  void *found = NULL;
  int n = 0, type;
  uint32_t off;
  for (size_t c = 0; c < sizeof COMPONENTS / sizeof COMPONENTS[0]; c++) {
    void *k = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, COMPONENTS[c]);
    if (k && rd_il2cpp_field_info(&g_il2cpp, k, p->e.key, &off, &type) && !n++) {
      found = k;
      snprintf(p->t.cls, sizeof p->t.cls, "%s", COMPONENTS[c]);
    }
  }
  if (n == 1) return found;
  debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: %s %s; write Class.%s\n", p->id, p->e.line,
              p->e.key, n ? "is in several car components" : "is no field of a car component",
              p->e.key);
  return NULL;
}

/* Offsets of the named float fields of klass into out; 1 if all exist. */
static int float_fields(void *klass, const char *const names[], int n, uint32_t out[]) {
  for (int i = 0; i < n; i++) {
    int type = 0;
    if (!rd_il2cpp_field_info(&g_il2cpp, klass, names[i], &out[i], &type) || type != RD_IL2CPP_TYPE_R4)
      return 0;
  }
  return 1;
}

/* Resolves one of cars.ini's own keys other than plain fields; 1 if it can be applied. */
static int resolve_car_key(patch *p) {
  uint32_t off[3];
  float f;
  char *end;
  const rd_car_kind kind = p->prop->kind;
  if (kind == RD_CAR_DRIVE) {
    p->v.type = RD_IL2CPP_TYPE_I4;
    p->v.v.i32 = rd_car_drive(p->e.value);
    p->method = g_il2cpp.class_get_method_from_name
                    ? (void *)g_il2cpp.class_get_method_from_name(p->klass, RD_MTH_SET_TRANSMISSION, 1)
                    : NULL;
    int type = 0;
    return p->v.v.i32 >= 0 && p->method && g_il2cpp.runtime_invoke &&
           rd_il2cpp_field_info(&g_il2cpp, p->klass, RD_FLD_TRANSMISSION, &p->offset, &type);
  }
  f = strtof(p->e.value, &end);
  if (end == p->e.value || *end || !(f >= 0)) return 0;
  p->v.type = RD_IL2CPP_TYPE_R4;
  p->v.v.f32 = f;
  if (kind == RD_CAR_GEAR) {   /* the array, and the index of first gear in it */
    int type = 0;
    return f > 0 && rd_il2cpp_field_info(&g_il2cpp, p->klass, RD_FLD_GEAR_RATIOS, &p->offset, &type) &&
           rd_il2cpp_field_info(&g_il2cpp, p->klass, RD_FLD_FIRST_GEAR, &p->aux[0], &type) &&
           type == RD_IL2CPP_TYPE_I4;
  }
  if (kind == RD_CAR_BASE) {
    const char *const names[] = { p->prop->field };
    return float_fields(p->klass, names, 1, &p->offset);
  }
  if (kind == RD_CAR_POWER || kind == RD_CAR_TORQUE) {
    const char *const names[] = { RD_FLD_POWER_MULTIPLIER,
                                  kind == RD_CAR_POWER ? RD_FLD_MAX_POWER : RD_FLD_MAX_TORQUE,
                                  RD_FLD_TUNING_MULTIPLIER };
    if (!float_fields(p->klass, names, 3, off)) return 0;
  } else {   /* RD_CAR_TURBO_BAR */
    const char *const names[] = { RD_FLD_TURBO_BAR, RD_FLD_TURBO_EXTRA, RD_FLD_TURBO_LIMIT };
    if (!float_fields(p->klass, names, 3, off)) return 0;
  }
  p->offset = off[0];
  p->aux[0] = off[1];
  p->aux[1] = off[2];
  return 1;
}

void rd_mods_patches_resolve(void) {
  if (g_il2cpp.resolve_icall) {
    g_transform = g_il2cpp.resolve_icall(RD_ICALL_TRANSFORM);
    g_parent = g_il2cpp.resolve_icall(RD_ICALL_PARENT);
  }
  for (int i = 0; i < g_npatches; i++) {
    patch *p = &g_patches[i];
    const char *file = p->car[0] ? CARS_INI : PATCH_INI;
    int type = 0;
    if (p->car[0])
      p->klass = p->t.cls[0] ? patch_class(&p->t) : car_component(p);
    else
      p->klass = rd_patch_target_parse(p->e.section, &p->t) ? NULL : patch_class(&p->t);
    if (!p->klass) {
      if (!p->car[0] || p->t.cls[0])
        debugPrintf(RD_LOG_TAG_MODS " %s: %s:%d: no class %s\n", p->id, file, p->e.line,
                    p->car[0] ? p->t.cls : p->e.section);
    } else if (p->prop && p->prop->kind != RD_CAR_FIELD) {
      p->ok = resolve_car_key(p);
      if (!p->ok)
        debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: %s = \"%s\" cannot be applied\n", p->id,
                    p->e.line, p->prop->key, p->e.value);
    } else if (!rd_il2cpp_field_info(&g_il2cpp, p->klass, p->e.key, &p->offset, &type)) {
      debugPrintf(RD_LOG_TAG_MODS " %s: %s:%d: no field %s in %s\n", p->id, file, p->e.line,
                  p->e.key, p->t.cls);
    } else {
      const int r = rd_patch_parse(type, p->e.value, &p->v);
      if (r == 0) p->ok = 1;
      else
        debugPrintf(RD_LOG_TAG_MODS " %s: %s:%d: %s = \"%s\": %s\n", p->id, file, p->e.line,
                    p->e.key, p->e.value, r == -2 ? "field type not supported" : "not a valid value");
    }
  }
}

/* ---- applying ---- */

/* The cars of the scene: the objects carrying a CarController (g_car_tf, g_car_name). */
static void find_cars(void) {
  static void *klass;
  static void *objs[MAX_CARS];
  if (!klass) klass = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_CAR_BASE);
  g_ncars = 0;
  if (!klass || !g_transform) return;
  int n = rd_mods_find_objects(klass, objs, MAX_CARS);
  if (n > MAX_CARS) n = MAX_CARS;
  for (int i = 0; i < n; i++) {
    g_car_tf[g_ncars] = g_transform(objs[i]);
    rd_mods_object_name(objs[i], g_car_name[g_ncars], RD_MODS_NAME_LEN);
    g_ncars += g_car_tf[g_ncars] != NULL;
  }
}

/* The name of the car obj is part of, or NULL if it is under none. */
static const char *car_of(void *obj) {
  void *t = g_transform ? g_transform(obj) : NULL;
  for (int depth = 0; t && depth < RD_CAR_PARENTS; depth++, t = g_parent ? g_parent(t) : NULL)
    for (int c = 0; c < g_ncars; c++)
      if (g_car_tf[c] == t) return g_car_name[c];
  return NULL;
}

/* Writes patch p into obj. */
static void write_patch(const patch *p, void *obj) {
  char *o = obj;
  float *at = (float *)(o + p->offset);
  switch (p->prop ? p->prop->kind : RD_CAR_FIELD) {
  case RD_CAR_FIELD:
    rd_patch_write(obj, p->offset, &p->v);
    break;
  case RD_CAR_DRIVE: {   /* the game reads it once (Start); rebuild the powered wheels on change */
    int32_t drive = p->v.v.i32;
    if (*(int32_t *)(o + p->offset) == drive) break;
    *(int32_t *)(o + p->offset) = drive;
    void *args[1] = { &drive };
    void *exc = NULL;
    g_il2cpp.runtime_invoke(p->method, obj, args, &exc);
    if (exc) debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: SetTransmission threw\n", p->id, p->e.line);
    break;
  }
  case RD_CAR_POWER:
  case RD_CAR_TORQUE: {
    const float m = rd_car_multiplier(p->v.v.f32, *(float *)(o + p->aux[0]), *(float *)(o + p->aux[1]));
    if (m > 0) *at = m;
    break;
  }
  case RD_CAR_TURBO_BAR:   /* Awake derives the limit once: limit = bar + extra */
    *at = p->v.v.f32;
    *(float *)(o + p->aux[1]) = p->v.v.f32 + *(float *)(o + p->aux[0]);
    break;
  case RD_CAR_GEAR: {
    void *gears = *(void **)(o + p->offset);
    const int32_t i = *(int32_t *)(o + p->aux[0]) + p->prop->index - 1;
    if (gears && g_il2cpp.array_length && i >= 0 && (uint32_t)i < g_il2cpp.array_length(gears))
      ((float *)((char *)gears + RD_IL2CPP_ARRAY_DATA))[i] = p->v.v.f32;
    break;
  }
  case RD_CAR_BASE:   /* apply_base writes these */
    break;
  }
}

static int is_base(const patch *p) { return p->prop && p->prop->kind == RD_CAR_BASE; }

/* ---- base values: the Vehicle records ---- */

/* Writes the base values into the matching Vehicle records of PersistentData.vehicles. The
 * first time, logs each record's names (what a [car] section matches) and every change. */
static void apply_base(void) {
  static void *data_class;
  static uint32_t off_vehicles, off_prefab, off_id, off_name;
  static int resolved, logged;
  if (!resolved) {
    resolved = 1;
    data_class = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_PERSISTENT);
    void *vehicle = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_VEHICLE);
    if (!data_class || !vehicle ||
        !rd_il2cpp_field_offset(&g_il2cpp, data_class, RD_FLD_VEHICLES, &off_vehicles) ||
        !rd_il2cpp_field_offset(&g_il2cpp, vehicle, RD_FLD_VEHICLE_PREFAB, &off_prefab) ||
        !rd_il2cpp_field_offset(&g_il2cpp, vehicle, RD_FLD_VEHICLE_ID, &off_id) ||
        !rd_il2cpp_field_offset(&g_il2cpp, vehicle, RD_FLD_VEHICLE_NAME, &off_name)) {
      debugPrintf(RD_LOG_TAG_MODS " cars.ini: no %s.%s; base values are not applied\n",
                  RD_CLS_PERSISTENT, RD_FLD_VEHICLES);
      data_class = NULL;
    }
  }
  void *data = NULL;
  if (!data_class || rd_mods_find_objects(data_class, &data, 1) < 1 || !data) return;
  void *list = *(void **)((char *)data + off_vehicles);
  const uint32_t n = list && g_il2cpp.array_length ? g_il2cpp.array_length(list) : 0;
  for (uint32_t v = 0; v < n; v++) {
    char *rec = rd_il2cpp_array_at(list, v);
    if (!rec) continue;
    char prefab[RD_MODS_NAME_LEN], id[RD_MODS_NAME_LEN], name[RD_MODS_NAME_LEN];
    rd_mods_ascii(*(void **)(rec + off_prefab), prefab, sizeof prefab);
    rd_mods_ascii(*(void **)(rec + off_id), id, sizeof id);
    rd_mods_ascii(*(void **)(rec + off_name), name, sizeof name);
    if (!logged) debugPrintf(RD_LOG_TAG_MODS " cars.ini: vehicle %u: prefab \"%s\", id \"%s\", name \"%s\"\n",
                             v, prefab, id, name);
    for (int i = 0; i < g_npatches; i++) {
      const patch *p = &g_patches[i];
      if (!p->ok || !is_base(p) || !rd_car_vehicle_is(p->car, prefab, id, name)) continue;
      float *at = (float *)(rec + p->offset);
      if (!logged && *at != p->v.v.f32)
        debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: %s of %s: %g -> %g\n", p->id, p->e.line,
                    p->prop->key, prefab, (double)*at, (double)p->v.v.f32);
      *at = p->v.v.f32;
    }
  }
  logged |= n > 0;
}

/* 1 if patch p applies to obj, whose name is `name` (patch.ini filters) and car `car`. */
static int applies(const patch *p, const char *name, const char *car) {
  if (p->car[0]) return car && rd_car_is(p->car, car);
  return !p->t.filter[0] || rd_name_match(p->t.filter, name);
}

void rd_mods_patches_apply(void) {
  static void *objs[MAX_OBJECTS];
  static char name[RD_MODS_NAME_LEN];
  int cars = 0, base = 0;
  for (int i = 0; i < g_npatches; i++) {
    cars |= g_patches[i].ok && g_patches[i].car[0] && !is_base(&g_patches[i]);
    base |= g_patches[i].ok && is_base(&g_patches[i]);
  }
  if (base) apply_base();
  if (cars) find_cars();
  for (int i = 0; i < g_npatches; i++) {
    if (!g_patches[i].ok || is_base(&g_patches[i])) continue;   /* Vehicle is no Unity object */
    int done = 0;   /* one object lookup per class */
    for (int j = 0; j < i; j++) done |= g_patches[j].ok && g_patches[j].klass == g_patches[i].klass;
    if (done) continue;
    int n = rd_mods_find_objects(g_patches[i].klass, objs, MAX_OBJECTS);
    if (n > MAX_OBJECTS) n = MAX_OBJECTS;
    int named = 0, carred = 0;   /* look names and cars up only when a patch needs them */
    for (int j = i; j < g_npatches; j++) {
      if (!g_patches[j].ok || g_patches[j].klass != g_patches[i].klass) continue;
      named |= !g_patches[j].car[0] && g_patches[j].t.filter[0];
      carred |= g_patches[j].car[0] != 0;
    }
    for (int o = 0; o < n; o++) {
      if (named) rd_mods_object_name(objs[o], name, sizeof name);
      const char *car = carred ? car_of(objs[o]) : NULL;
      /* in load order: with two patches of one field the later one is written last */
      for (int j = i; j < g_npatches; j++) {
        const patch *p = &g_patches[j];
        if (p->ok && p->klass == g_patches[i].klass && applies(p, name, car)) write_patch(p, objs[o]);
      }
    }
  }
}
