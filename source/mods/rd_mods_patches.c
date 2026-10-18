/* rd_mods_patches.c -- patch.ini and cars.ini values written into the game's live objects.
 * Loaded at boot (rd_mods.c), resolved once the game is ready, applied after each scene change
 * and every RD_MODS_PATCH_FRAMES frames, and -- for cars.ini -- right before the game builds a
 * car (prefix hooks on MenuManager.LoadOriginalValues and RaceManager.UpdateVehicle). Main
 * thread.
 *
 * A value is one patch: a class, how to write it (a field, or one of cars.ini's own keys:
 * rd_mod_cars.h) and which objects (all, a patch.ini "@ filter" of object names, or a car).
 * A car object carries a Drivetrain (race car, garage and menu models); an object belongs to
 * the car object it is on or under (wheels are child objects). Each cars.ini key is written
 * when the game reads it (rd_car_when): every pass, on scene change, or as the car's originals
 * just before the game copies them into its Vehicle record and applies the player's tuning.
 * Derived values come from the game's own methods (SetTransmission, CalcMaxPowerMaxTorque,
 * SetWheelsParams, SetBrakes, SetTiresType, AnimationCurve.SetKeys / SmoothTangents).
 * MIT (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_game_symbols.h"
#include "rd_hook.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_mod_cars.h"
#include "rd_mod_patch.h"
#include "rd_mods_internal.h"
#include "util.h"

#define PATCH_INI     "patch.ini"
#define CARS_INI      "cars.ini"
#define MAX_PATCHES   256
#define MAX_OBJECTS   256         /* objects of one class patched per pass */
#define MAX_CARS      32          /* car objects in one scene */
#define KEYFRAME_SIZE 28          /* UnityEngine.Keyframe: time value in out mode inW outW */
#define KEY_WEIGHT    (1.0f / 3)  /* Unity's default tangent weight */

/* Which timings a pass writes (rd_car_when as bits). */
#define PASS_LIVE  (1 << RD_CAR_LIVE)
#define PASS_SCENE (PASS_LIVE | 1 << RD_CAR_ONCE)
#define PASS_BUILD (PASS_SCENE | 1 << RD_CAR_BUILD)

/* After writing into a CarDynamics' axles: the game's methods that pass them on. */
#define AXLES_WHEELS 1   /* SetWheelsParams: suspension, grip, pressure, camber, steering */
#define AXLES_BRAKES 2   /* SetBrakes: brake torques and balance */
#define AXLES_TIRES  4   /* SetTiresType: the tire data of Axle.tires */

typedef struct {
  const char      *id;           /* the mod's, for log lines */
  rd_ini_entry     e;            /* patch.ini: section = class; key = field, value */
  rd_patch_target  t;            /* the class; t.filter limits patch.ini values to object names */
  char             car[RD_INI_SECTION_LEN];   /* cars.ini: the car ("*" = all), "" = patch.ini */
  const rd_car_prop *prop;       /* cars.ini: its own key, NULL for a field */
  void            *klass;
  uint32_t         offset;
  uint32_t         aux;          /* a second field: maxPower, first gear, turbo extra */
  uint32_t         aux2;         /* a third: the turbo limit */
  void            *method;       /* SetTransmission / CalcMaxPowerMaxTorque / the set_mass icall */
  rd_patch_value   v;
  float            rpm[RD_CAR_CURVE_MAX], nm[RD_CAR_CURVE_MAX];   /* torque_curve; gears: rpm[] */
  int              nkeys;        /* the curve's keys, or the number of gears */
  int              ok;
  int              logged;       /* the first write was logged with the car's own value */
} patch;

static patch *g_patches;
static int    g_npatches;

/* Car objects of the current pass: their transform and name. */
static void *g_car_tf[MAX_CARS];
static char  g_car_name[MAX_CARS][RD_MODS_NAME_LEN];
static int   g_ncars;

/* The car being built (a hook's MenuManager / RaceManager fields), NULL outside the hooks:
 * a garage model may not be found by FindObjectsOfType yet. */
static void *g_build_drivetrain, *g_build_dynamics, *g_build_body;

/* Resolved once (rd_mods_patches_resolve). */
static void *(*g_transform)(void *component);
static void *(*g_parent)(void *transform);
static void  *g_drivetrain_class, *g_dynamics_class;
static uint32_t g_off_axles, g_off_front, g_off_rear, g_off_body;
static void  *g_set_wheels, *g_set_brakes, *g_set_tires;   /* CarDynamics MethodInfos */
static void  *g_keyframe_class;
static void (*g_set_keys)(void *curve, void *keys);
static void (*g_smooth)(void *curve, int32_t index, float weight);
static float (*g_get_mass)(void *body);
static void  *g_single_class;      /* System.Single: elements of a new gearRatios */
static uint32_t g_off_gears;       /* Drivetrain.gearRatios, for the gear tuning guard */

/* A new patch of mod `id`, or NULL (logged) when the table is full. */
static patch *add_patch(const char *id) {
  if (!g_patches) g_patches = calloc(MAX_PATCHES, sizeof *g_patches);
  if (!g_patches || g_npatches == MAX_PATCHES) {
    debugPrintf(RD_LOG_TAG_MODS " %s: too many patch and car values\n", id);
    return NULL;
  }
  patch *p = &g_patches[g_npatches++];
  memset(p, 0, sizeof *p);
  p->id = id;
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

/* A MethodInfo of klass, or NULL. */
static void *method(void *klass, const char *name, int argc) {
  return klass && g_il2cpp.class_get_method_from_name
             ? (void *)g_il2cpp.class_get_method_from_name(klass, name, argc) : NULL;
}

/* The offset of a field of klass of RD_IL2CPP_TYPE_* `type` (0: any); 1 if found. */
static int field_of(void *klass, const char *name, int type, uint32_t *off) {
  int t = 0;
  return klass && rd_il2cpp_field_info(&g_il2cpp, klass, name, off, &t) && (!type || t == type);
}

/* Resolves the classes, fields and methods cars.ini needs beyond the patches' own. */
static void resolve_cars(void) {
  g_drivetrain_class = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_DRIVETRAIN);
  g_dynamics_class = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_CAR_DYNAMICS);
  void *axles = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_AXLES);
  if (!field_of(g_dynamics_class, RD_FLD_AXLES, 0, &g_off_axles) ||
      !field_of(axles, RD_FLD_FRONT_AXLE, 0, &g_off_front) ||
      !field_of(axles, RD_FLD_REAR_AXLE, 0, &g_off_rear))
    g_off_axles = 0;
  if (!field_of(g_drivetrain_class, RD_FLD_DRIVETRAIN_BODY, 0, &g_off_body)) g_off_body = 0;
  g_set_wheels = method(g_dynamics_class, RD_MTH_SET_WHEELS_PARAMS, 0);
  g_set_brakes = method(g_dynamics_class, RD_MTH_SET_BRAKES, 0);
  g_set_tires = method(g_dynamics_class, RD_MTH_SET_TIRES_TYPE, 0);
  g_keyframe_class = rd_il2cpp_class(&g_il2cpp, RD_ASM_UNITY_CORE, RD_NS_UNITY, RD_CLS_KEYFRAME);
  g_single_class = rd_il2cpp_class(&g_il2cpp, RD_ASM_CORLIB, RD_NS_SYSTEM, RD_CLS_SINGLE);
  if (!field_of(g_drivetrain_class, RD_FLD_GEAR_RATIOS, 0, &g_off_gears)) g_off_gears = 0;
  if (g_il2cpp.resolve_icall) {
    g_set_keys = g_il2cpp.resolve_icall(RD_ICALL_CURVE_SET_KEYS);
    g_smooth = g_il2cpp.resolve_icall(RD_ICALL_CURVE_SMOOTH);
    g_get_mass = g_il2cpp.resolve_icall(RD_ICALL_GET_MASS);
  }
}

/* A float value of one of the table's keys into p->v; 1 if valid (`positive`: above 0). */
static int float_value(patch *p, int positive) {
  char *end;
  const float f = strtof(p->e.value, &end);
  p->v.type = RD_IL2CPP_TYPE_R4;
  p->v.v.f32 = f;
  return end != p->e.value && !*end && (positive ? f > 0 : f >= 0);
}

/* Resolves one of cars.ini's own keys (not a plain field); 1 if it can be applied. */
static int resolve_car_key(patch *p) {
  const rd_car_prop *k = p->prop;
  int type = 0;
  switch (k->kind) {
  case RD_CAR_AXLE: {
    void *axle = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_AXLE);
    if (!g_off_axles || !rd_il2cpp_field_info(&g_il2cpp, axle, k->field, &p->offset, &type)) return 0;
    p->klass = g_dynamics_class;   /* the axles are reached through the car's CarDynamics */
    if (k->names) {
      p->v.type = RD_IL2CPP_TYPE_I4;
      p->v.v.i32 = rd_car_enum(k->names, p->e.value);
      return p->v.v.i32 >= 0;
    }
    return rd_patch_parse(type, p->e.value, &p->v) == 0;
  }
  case RD_CAR_DRIVE:
    p->v.type = RD_IL2CPP_TYPE_I4;
    p->v.v.i32 = rd_car_enum(k->names, p->e.value);
    p->method = method(p->klass, RD_MTH_SET_TRANSMISSION, 1);
    return p->v.v.i32 >= 0 && p->method && g_il2cpp.runtime_invoke &&
           field_of(p->klass, RD_FLD_TRANSMISSION, 0, &p->offset);
  case RD_CAR_POWER:
    p->method = method(p->klass, RD_MTH_CALC_MAX_POWER, 2);
    return float_value(p, 1) && p->method && g_il2cpp.runtime_invoke &&
           field_of(p->klass, RD_FLD_POWER_MULTIPLIER, RD_IL2CPP_TYPE_R4, &p->offset) &&
           field_of(p->klass, RD_FLD_MAX_POWER, RD_IL2CPP_TYPE_R4, &p->aux);
  case RD_CAR_CURVE:
    p->nkeys = rd_car_curve_parse(p->e.value, p->rpm, p->nm, RD_CAR_CURVE_MAX);
    p->method = method(p->klass, RD_MTH_CALC_MAX_POWER, 2);
    return p->nkeys >= 2 && g_set_keys && g_smooth && g_keyframe_class && g_il2cpp.array_new &&
           field_of(p->klass, RD_FLD_TORQUE_CURVE, 0, &p->offset) &&
           field_of(p->klass, RD_FLD_POWER_MULTIPLIER, RD_IL2CPP_TYPE_R4, &p->aux);
  case RD_CAR_TURBO_BAR:
    return float_value(p, 0) && field_of(p->klass, RD_FLD_TURBO_BAR, RD_IL2CPP_TYPE_R4, &p->offset) &&
           field_of(p->klass, RD_FLD_TURBO_EXTRA, RD_IL2CPP_TYPE_R4, &p->aux) &&
           field_of(p->klass, RD_FLD_TURBO_LIMIT, RD_IL2CPP_TYPE_R4, &p->aux2);
  case RD_CAR_GEARS:
    p->nkeys = rd_car_gears_parse(p->e.value, p->rpm, RD_CAR_GEARS_MAX);
    return p->nkeys >= 1 && g_single_class && g_il2cpp.array_new && g_il2cpp.array_length &&
           field_of(p->klass, RD_FLD_GEAR_RATIOS, 0, &p->offset) &&
           field_of(p->klass, RD_FLD_FIRST_GEAR, RD_IL2CPP_TYPE_I4, &p->aux);
  case RD_CAR_MASS:
    p->method = g_il2cpp.resolve_icall ? g_il2cpp.resolve_icall(RD_ICALL_SET_MASS) : NULL;
    return float_value(p, 1) && p->method && g_off_body;
  case RD_CAR_FIELD:
    break;
  }
  return 0;
}

/* ---- hooks: the car's originals right before the game reads them ---- */

static void (*g_orig_load_originals)(void *self, void *mi);
static void (*g_orig_update_vehicle)(void *self, void *mi);
static uint32_t g_off_menu_dt, g_off_menu_cd, g_off_menu_body, g_off_race_dt, g_off_race_cd;

static void apply(int mask);

/* Writes every cars.ini value into the car being built (the manager's fields), then lets the
 * game read it. */
static void build_car(void *dt, void *cd, void *body) {
  g_build_drivetrain = dt;
  g_build_dynamics = cd;
  g_build_body = body;
  apply(PASS_BUILD);
  g_build_drivetrain = g_build_dynamics = g_build_body = NULL;
}

static void *field_ptr(void *obj, uint32_t off) { return obj && off ? *(void **)((char *)obj + off) : NULL; }

static void hook_load_originals(void *self, void *mi) {   /* MenuManager.LoadOriginalValues */
  build_car(field_ptr(self, g_off_menu_dt), field_ptr(self, g_off_menu_cd), field_ptr(self, g_off_menu_body));
  g_orig_load_originals(self, mi);
}

static void hook_update_vehicle(void *self, void *mi) {   /* RaceManager.UpdateVehicle */
  build_car(field_ptr(self, g_off_race_dt), field_ptr(self, g_off_race_cd), NULL);
  g_orig_update_vehicle(self, mi);
}

static void (*g_orig_gear_tuning)(void *self, void *dt, int32_t car, int32_t gear, float ratio, void *mi);

/* PersistentData.ApplyGearRatiosTuning, guarded: the player's saved tuning of a gear the car no
 * longer has (after `gears` made it shorter) is skipped instead of throwing out of the game's
 * car building. Gear 0 is the final drive; -1 and 0 are the game's "not tuned". */
static void hook_gear_tuning(void *self, void *dt, int32_t car, int32_t gear, float ratio, void *mi) {
  void *gears = field_ptr(dt, g_off_gears);
  if (gear > 0 && ratio != -1 && ratio != 0 && gears && (uint32_t)gear + 1 >= g_il2cpp.array_length(gears))
    return;
  g_orig_gear_tuning(self, dt, car, gear, ratio, mi);
}

/* Hooks the method `name` of class `cls` with fn; 1 on success (logged otherwise). */
static int hook(const char *cls, const char *name, int argc, void *fn, void **orig) {
  void *k = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, cls);
  void *mi = method(k, name, argc);
  const int r = mi ? rd_hook_install(*(void **)mi, fn, orig) : -3;
  if (r) debugPrintf(RD_LOG_TAG_MODS " " CARS_INI ": cannot hook %s.%s (%d)\n", cls, name, r);
  return r == 0;
}

/* The two prefix hooks, the managers' car fields; only when some cars.ini value is a build key. */
static void install_hooks(void) {
  int build = 0;
  for (int i = 0; i < g_npatches; i++)
    build |= g_patches[i].ok && g_patches[i].car[0] && (!g_patches[i].prop || g_patches[i].prop->when != RD_CAR_LIVE);
  if (!build) return;
  void *menu = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_MENU_MANAGER);
  void *race = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_RACE_MANAGER);
  field_of(menu, RD_FLD_MANAGER_DRIVETRAIN, 0, &g_off_menu_dt);
  field_of(menu, RD_FLD_MANAGER_DYNAMICS, 0, &g_off_menu_cd);
  field_of(menu, RD_FLD_MANAGER_BODY, 0, &g_off_menu_body);
  field_of(race, RD_FLD_MANAGER_DRIVETRAIN, 0, &g_off_race_dt);
  field_of(race, RD_FLD_MANAGER_DYNAMICS, 0, &g_off_race_cd);
  const int a = hook(RD_CLS_MENU_MANAGER, RD_MTH_LOAD_ORIGINALS, 0, (void *)hook_load_originals,
                     (void **)&g_orig_load_originals);
  const int b = hook(RD_CLS_RACE_MANAGER, RD_MTH_UPDATE_VEHICLE, 0, (void *)hook_update_vehicle,
                     (void **)&g_orig_update_vehicle);
  if (a && b) debugPrintf(RD_LOG_TAG_MODS " " CARS_INI ": hooked %s, %s\n", RD_MTH_LOAD_ORIGINALS,
                          RD_MTH_UPDATE_VEHICLE);
  int gears = 0;
  for (int i = 0; i < g_npatches; i++)
    gears |= g_patches[i].ok && g_patches[i].prop && g_patches[i].prop->kind == RD_CAR_GEARS;
  if (gears && g_off_gears && g_il2cpp.array_length &&
      hook(RD_CLS_PERSISTENT_DATA, RD_MTH_APPLY_GEAR_TUNING, 4, (void *)hook_gear_tuning,
           (void **)&g_orig_gear_tuning))
    debugPrintf(RD_LOG_TAG_MODS " " CARS_INI ": guarded %s\n", RD_MTH_APPLY_GEAR_TUNING);
}

void rd_mods_patches_resolve(void) {
  if (g_il2cpp.resolve_icall) {
    g_transform = g_il2cpp.resolve_icall(RD_ICALL_TRANSFORM);
    g_parent = g_il2cpp.resolve_icall(RD_ICALL_PARENT);
  }
  int cars = 0;
  for (int i = 0; i < g_npatches; i++) cars |= g_patches[i].car[0] != 0;
  if (cars) resolve_cars();
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
  if (cars) install_hooks();
}

/* ---- applying ---- */

/* Adds a car object (its transform and name) to the pass's car list. */
static void add_car(void *component) {
  if (g_ncars == MAX_CARS || !component || !g_transform) return;
  g_car_tf[g_ncars] = g_transform(component);
  rd_mods_object_name(component, g_car_name[g_ncars], RD_MODS_NAME_LEN);
  g_ncars += g_car_tf[g_ncars] != NULL;
}

/* The car objects of the scene (carriers of a Drivetrain), and the one being built. */
static void find_cars(void) {
  static void *objs[MAX_CARS];
  g_ncars = 0;
  add_car(g_build_drivetrain);
  int n = g_drivetrain_class ? rd_mods_find_objects(g_drivetrain_class, objs, MAX_CARS) : 0;
  if (n > MAX_CARS) n = MAX_CARS;
  for (int i = 0; i < n; i++)
    if (objs[i] != g_build_drivetrain) add_car(objs[i]);
}

/* The name of the car object obj is part of, or NULL if it is under none. */
static const char *car_of(void *obj) {
  void *t = g_transform ? g_transform(obj) : NULL;
  for (int depth = 0; t && depth < RD_CAR_PARENTS; depth++, t = g_parent ? g_parent(t) : NULL)
    for (int c = 0; c < g_ncars; c++)
      if (g_car_tf[c] == t) return g_car_name[c];
  return NULL;
}

/* Logs the first write of cars.ini patch p: the car's own value and the new one, so a modder
 * learns the game's values and units from debug.log. */
static void log_first(patch *p, const char *car, const char *old, const char *now) {
  if (p->logged || !p->car[0]) return;
  p->logged = 1;
  debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: %s of %s: %s -> %s\n", p->id, p->e.line,
              p->prop ? p->prop->key : p->e.key, car ? car : "?", old, now);
}

/* Logs the first write of a value written like a field (old value read as v's type). */
static void log_field(patch *p, const char *car, const void *obj, uint32_t off) {
  char old[32];
  if (p->logged || !p->car[0]) return;
  rd_patch_format(obj, off, &p->v, old, sizeof old);
  log_first(p, car, old, p->e.value);
}

/* Calls a MethodInfo with arguments; 0, or -1 if it threw (logged for patch p). */
static int invoke(const patch *p, void *mi, void *obj, void **args) {
  void *exc = NULL;
  g_il2cpp.runtime_invoke(mi, obj, args, &exc);
  if (exc) debugPrintf(RD_LOG_TAG_MODS " %s: " CARS_INI ":%d: %s threw\n", p->id, p->e.line, p->prop->key);
  return exc ? -1 : 0;
}

/* The game's scan of the torque curve at `factor` into maxPower / maxTorque and their rpm. */
static int calc_max_power(const patch *p, void *dt, float factor) {
  float turbo = -1;   /* the game's "no override" */
  void *args[2] = { &factor, &turbo };
  return invoke(p, p->method, dt, args);
}

/* Replaces the curve's keys (rpm -> Nm) and smooths them as the Unity editor does. */
static void write_curve(const patch *p, void *dt) {
  void *curve = field_ptr(dt, p->offset);
  void *keys = curve ? g_il2cpp.array_new(g_keyframe_class, (uintptr_t)p->nkeys) : NULL;
  if (!keys) return;
  for (int i = 0; i < p->nkeys; i++) {
    float *k = (float *)((char *)keys + RD_IL2CPP_ARRAY_DATA + KEYFRAME_SIZE * i);
    k[0] = p->rpm[i];
    k[1] = p->nm[i];
    k[2] = k[3] = 0;
    ((int32_t *)k)[4] = 0;   /* WeightedMode.None */
    k[5] = k[6] = KEY_WEIGHT;
  }
  g_set_keys(curve, keys);
  for (int i = 0; i < p->nkeys; i++) g_smooth(curve, i, 0);
  if (p->method) calc_max_power(p, dt, *(float *)((char *)dt + p->aux));   /* the specs shown */
}

/* Writes the power target: the multiplier from the curve's own peak, then the
 * game's maxPower / maxTorque as it would compute them at that multiplier (the specs screen). */
static void write_power(patch *p, void *dt, const char *car) {
  if (calc_max_power(p, dt, 1)) return;
  const float peak = *(float *)((char *)dt + p->aux);
  const float m = rd_car_multiplier(p->v.v.f32, peak);
  char old[32];
  snprintf(old, sizeof old, "%g (curve peak)", (double)peak);
  log_first(p, car, old, p->e.value);
  if (m <= 0) return;
  *(float *)((char *)dt + p->offset) = m;
  calc_max_power(p, dt, m);
}

/* Replaces the forward gears of Drivetrain dt: a new gearRatios with the entries before `first`
 * (reverse, neutral) kept and the mod's ratios after them -- the array's length is the number
 * of gears to the game (ShiftGear, LoadOriginalValues). */
static void write_gears(patch *p, void *dt, const char *car) {
  void *old = field_ptr(dt, p->offset);
  const int32_t first = *(int32_t *)((char *)dt + p->aux);
  const uint32_t len = old ? g_il2cpp.array_length(old) : 0;
  if (!old || first < 0 || (uint32_t)first > len) return;
  const float *was = (const float *)((char *)old + RD_IL2CPP_ARRAY_DATA);
  if (len == (uint32_t)(first + p->nkeys) && !memcmp(was + first, p->rpm, sizeof(float) * (size_t)p->nkeys))
    return;   /* already these gears */
  void *now = g_il2cpp.array_new(g_single_class, (uintptr_t)(first + p->nkeys));
  if (!now) return;
  float *ratios = (float *)((char *)now + RD_IL2CPP_ARRAY_DATA);
  memcpy(ratios, was, sizeof(float) * (size_t)first);
  memcpy(ratios + first, p->rpm, sizeof(float) * (size_t)p->nkeys);
  char olds[32];
  snprintf(olds, sizeof olds, "%u gears", len - (uint32_t)first);
  log_first(p, car, olds, p->e.value);
  *(void **)((char *)dt + p->offset) = now;
}

/* Writes an Axle value into the front and/or rear axle of CarDynamics cd; the AXLES_* to pass
 * on, 0 if nothing changed. */
static int write_axles(patch *p, void *cd, const char *car) {
  void *axles = field_ptr(cd, g_off_axles);
  int changed = 0;
  for (int which = RD_AXLE_FRONT; which <= RD_AXLE_REAR; which <<= 1) {
    void *axle = (p->prop->axle & which) ? field_ptr(axles, which == RD_AXLE_FRONT ? g_off_front : g_off_rear) : NULL;
    if (axle && !rd_patch_same(axle, p->offset, &p->v)) {
      log_field(p, car, axle, p->offset);
      rd_patch_write(axle, p->offset, &p->v);
      changed = 1;
    }
  }
  if (!changed) return 0;
  if (!strcmp(p->prop->field, RD_FLD_TIRES)) return AXLES_TIRES | AXLES_WHEELS;
  if (!strcmp(p->prop->field, RD_FLD_BRAKE_TORQUE) || !strcmp(p->prop->field, RD_FLD_HANDBRAKE_TORQUE))
    return AXLES_BRAKES;
  return AXLES_WHEELS;
}

/* Writes patch p into obj of car `car`; the AXLES_* the caller must pass on for obj (a
 * CarDynamics). */
static int write_patch(patch *p, void *obj, const char *car) {
  char *o = obj;
  float *at = (float *)(o + p->offset);
  switch (p->prop ? p->prop->kind : RD_CAR_FIELD) {
  case RD_CAR_FIELD: {
    const int brakes = p->prop && (!strcmp(p->prop->field, RD_FLD_BRAKE_BALANCE) ||
                                   !strcmp(p->prop->field, RD_FLD_HANDBRAKE_BALANCE));
    if (rd_patch_same(obj, p->offset, &p->v)) return 0;
    log_field(p, car, obj, p->offset);
    rd_patch_write(obj, p->offset, &p->v);
    return brakes ? AXLES_BRAKES : 0;
  }
  case RD_CAR_AXLE:
    return write_axles(p, obj, car);
  case RD_CAR_DRIVE: {   /* the game reads it once (Start); SetTransmission picks the wheels */
    int32_t drive = p->v.v.i32;
    if (*(int32_t *)(o + p->offset) == drive) return 0;
    *(int32_t *)(o + p->offset) = drive;
    void *args[1] = { &drive };
    invoke(p, p->method, obj, args);
    return 0;
  }
  case RD_CAR_POWER:
    write_power(p, obj, car);
    return 0;
  case RD_CAR_CURVE:
    write_curve(p, obj);
    return 0;
  case RD_CAR_TURBO_BAR:   /* Awake derives the limit once: limit = bar + extra */
    log_field(p, car, obj, p->offset);
    *at = p->v.v.f32;
    *(float *)(o + p->aux2) = p->v.v.f32 + *(float *)(o + p->aux);
    return 0;
  case RD_CAR_GEARS:
    write_gears(p, obj, car);
    return 0;
  case RD_CAR_MASS: {
    void *body = field_ptr(obj, g_off_body);
    if (!body && obj == g_build_drivetrain) body = g_build_body;
    if (body && g_get_mass && !p->logged) {
      char old[32];
      snprintf(old, sizeof old, "%g", (double)g_get_mass(body));
      log_first(p, car, old, p->e.value);
    }
    if (body) ((void (*)(void *, float))p->method)(body, p->v.v.f32);
    return 0;
  }
  }
  return 0;
}

/* Passes axle changes on to the wheels with the game's own methods. */
static void pass_axles(const patch *p, void *cd, int what) {
  if ((what & AXLES_TIRES) && g_set_tires) invoke(p, g_set_tires, cd, NULL);
  if ((what & AXLES_WHEELS) && g_set_wheels) invoke(p, g_set_wheels, cd, NULL);
  if ((what & AXLES_BRAKES) && g_set_brakes) invoke(p, g_set_brakes, cd, NULL);
}

/* 1 if patch p is written in a pass of `mask` into obj, whose name is `name` (patch.ini
 * filters) and car `car`. */
static int applies(const patch *p, int mask, const char *name, const char *car) {
  if (!p->car[0]) return (mask & PASS_LIVE) && (!p->t.filter[0] || rd_name_match(p->t.filter, name));
  const rd_car_when when = p->prop ? p->prop->when : RD_CAR_LIVE;
  return (mask & (1 << when)) && car && rd_car_is(p->car, car);
}

/* The objects of klass for this pass: the car being built (its Drivetrain or CarDynamics) and
 * the live objects. Returns how many. */
static int objects_of(void *klass, void **out, int max) {
  void *built = klass == g_drivetrain_class ? g_build_drivetrain
              : klass == g_dynamics_class ? g_build_dynamics : NULL;
  const int first = built != NULL;
  if (built) out[0] = built;
  int m = rd_mods_find_objects(klass, out + first, max - first);
  if (m > max - first) m = max - first;
  int n = first;
  for (int i = 0; i < m; i++)   /* the built one only once; n <= first + i, so in place */
    if (out[first + i] != built) out[n++] = out[first + i];
  return n;
}

/* One object: every applying patch of its class, the torque curve before power. */
static void patch_object(int first, void *obj, int mask, const char *name, const char *car) {
  const patch *last = NULL;
  int axles = 0;
  for (int order = 0; order < 2; order++)
    for (int j = first; j < g_npatches; j++) {
      patch *p = &g_patches[j];
      if (!p->ok || p->klass != g_patches[first].klass || !applies(p, mask, name, car)) continue;
      if ((p->prop && p->prop->kind == RD_CAR_CURVE) != (order == 0)) continue;
      const int w = write_patch(p, obj, car);
      if (w) { axles |= w; last = p; }
    }
  if (axles) pass_axles(last, obj, axles);
}

/* One pass over every patch: the timings of `mask` (PASS_*). */
static void apply(int mask) {
  static void *objs[MAX_OBJECTS];
  static char name[RD_MODS_NAME_LEN];
  int cars = 0;
  for (int i = 0; i < g_npatches; i++) cars |= g_patches[i].ok && g_patches[i].car[0];
  if (cars) find_cars();
  for (int i = 0; i < g_npatches; i++) {
    if (!g_patches[i].ok) continue;
    int done = 0;   /* one object lookup per class */
    for (int j = 0; j < i; j++) done |= g_patches[j].ok && g_patches[j].klass == g_patches[i].klass;
    if (done) continue;
    const int n = objects_of(g_patches[i].klass, objs, MAX_OBJECTS);
    int named = 0, carred = 0;   /* look names and cars up only when a patch needs them */
    for (int j = i; j < g_npatches; j++) {
      if (!g_patches[j].ok || g_patches[j].klass != g_patches[i].klass) continue;
      named |= !g_patches[j].car[0] && g_patches[j].t.filter[0];
      carred |= g_patches[j].car[0] != 0;
    }
    for (int o = 0; o < n; o++) {
      if (named) rd_mods_object_name(objs[o], name, sizeof name);
      patch_object(i, objs[o], mask, name, carred ? car_of(objs[o]) : NULL);
    }
  }
}

void rd_mods_patches_apply(int scene_changed) {
  apply(scene_changed ? PASS_SCENE : PASS_LIVE);
}
