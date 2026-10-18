/* rd_mod_cars.c -- see rd_mod_cars.h. MIT (see LICENSE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "rd_game_symbols.h"
#include "rd_mod_cars.h"

static const char *const DRIVES[] = RD_DRIVES;
static const char *const DIFFS[] = RD_DIFFS;
static const char *const TIRES[] = RD_TIRES;
static const char *const SUFFIXES[] = RD_CAR_NAME_SUFFIXES;

#define F  RD_AXLE_FRONT
#define R  RD_AXLE_REAR
#define DT RD_CLS_DRIVETRAIN
#define AX RD_CLS_AXLE
#define CD RD_CLS_CAR_DYNAMICS
#define CC RD_CLS_CAR_BASE
#define DIFF_NAMES "open viscous locked one_way_LSD two_way_LSD one_point_five_way_LSD"

/* Where each value lives and when the game reads it: analysis/research/14-modding.md. */
static const rd_car_prop PROPS[] = {
  /* engine */
  { "power",        RD_CAR_POWER,     RD_CAR_ONCE,  DT, RD_FLD_POWER_MULTIPLIER, "hp, stock peak", 0, 0, NULL },
  { "torque",       RD_CAR_TORQUE,    RD_CAR_ONCE,  DT, RD_FLD_POWER_MULTIPLIER, "Nm, stock peak", 0, 0, NULL },
  { "torque_curve", RD_CAR_CURVE,     RD_CAR_ONCE,  DT, RD_FLD_TORQUE_CURVE,     "rpm:Nm pairs", 0, 0, NULL },
  { "turbo",        RD_CAR_FIELD,     RD_CAR_LIVE,  DT, RD_FLD_TURBO,            "0 / 1", 0, 0, NULL },
  { "turbo_bar",    RD_CAR_TURBO_BAR, RD_CAR_LIVE,  DT, RD_FLD_TURBO_BAR,        "bar", 0, 0, NULL },
  { "rev_limit",    RD_CAR_FIELD,     RD_CAR_LIVE,  DT, RD_FLD_REV_LIMIT,        "rpm", 0, 0, NULL },
  { "max_rpm",      RD_CAR_FIELD,     RD_CAR_LIVE,  DT, RD_FLD_MAX_RPM,          "rpm", 0, 0, NULL },
  { "idle_rpm",     RD_CAR_FIELD,     RD_CAR_LIVE,  DT, RD_FLD_IDLE_RPM,         "rpm", 0, 0, NULL },
  /* drivetrain */
  { "drive",        RD_CAR_DRIVE,     RD_CAR_LIVE,  DT, RD_FLD_TRANSMISSION,     "RWD FWD AWD XWD", 0, 0, DRIVES },
  { "shift_time",   RD_CAR_FIELD,     RD_CAR_BUILD, DT, RD_FLD_SHIFT_TIME,       "s", 0, 0, NULL },
  { "final_ratio",  RD_CAR_FIELD,     RD_CAR_BUILD, DT, RD_FLD_FINAL_RATIO,      "ratio", 0, 0, NULL },
  { "gear_1",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 1, 0, NULL },
  { "gear_2",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 2, 0, NULL },
  { "gear_3",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 3, 0, NULL },
  { "gear_4",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 4, 0, NULL },
  { "gear_5",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 5, 0, NULL },
  { "gear_6",       RD_CAR_GEAR,      RD_CAR_BUILD, DT, RD_FLD_GEAR_RATIOS,      "ratio", 6, 0, NULL },
  { "front_diff",         RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFFERENTIAL, DIFF_NAMES, 0, F, DIFFS },
  { "rear_diff",          RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFFERENTIAL, DIFF_NAMES, 0, R, DIFFS },
  { "front_diff_preload", RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_PRELOAD, "Nm", 0, F, NULL },
  { "rear_diff_preload",  RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_PRELOAD, "Nm", 0, R, NULL },
  { "front_diff_power",   RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_POWER,   "LSD ratio on power", 0, F, NULL },
  { "rear_diff_power",    RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_POWER,   "LSD ratio on power", 0, R, NULL },
  { "front_diff_coast",   RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_COAST,   "LSD ratio off power", 0, F, NULL },
  { "rear_diff_coast",    RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_DIFF_COAST,   "LSD ratio off power", 0, R, NULL },
  /* body */
  { "weight",       RD_CAR_MASS,  RD_CAR_BUILD, DT, RD_FLD_DRIVETRAIN_BODY, "kg", 0, 0, NULL },
  { "weight_distr", RD_CAR_FIELD, RD_CAR_BUILD, CD, RD_FLD_COM_Z,           "centre of mass, front-rear", 0, 0, NULL },
  /* suspension and steering */
  { "susp_height",    RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_SUSP_TRAVEL,    "suspension travel", 0, F | R, NULL },
  { "front_camber",   RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_CAMBER,         "degrees", 0, F, NULL },
  { "rear_camber",    RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_CAMBER,         "degrees", 0, R, NULL },
  { "steering_angle", RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_STEERING_ANGLE, "degrees", 0, F, NULL },
  { "front_spring",   RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_RATE,      "spring rate", 0, F, NULL },
  { "rear_spring",    RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_RATE,      "spring rate", 0, R, NULL },
  { "front_bump",     RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_BUMP,      "damper rate", 0, F, NULL },
  { "rear_bump",      RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_BUMP,      "damper rate", 0, R, NULL },
  { "front_rebound",  RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_REBOUND,   "damper rate", 0, F, NULL },
  { "rear_rebound",   RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_SUSP_REBOUND,   "damper rate", 0, R, NULL },
  { "front_antiroll", RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_ANTIROLL,       "anti-roll bar rate", 0, F, NULL },
  { "rear_antiroll",  RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_ANTIROLL,       "anti-roll bar rate", 0, R, NULL },
  /* tires and grip */
  { "front_grip",      RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_FORWARD_GRIP,  "factor", 0, F, NULL },
  { "front_side_grip", RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_SIDEWAYS_GRIP, "factor", 0, F, NULL },
  { "rear_grip",       RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_FORWARD_GRIP,  "factor", 0, R, NULL },
  { "rear_side_grip",  RD_CAR_AXLE, RD_CAR_BUILD, AX, RD_FLD_SIDEWAYS_GRIP, "factor", 0, R, NULL },
  { "front_tires",     RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_TIRES,         "a CarDynamics.Tires name", 0, F, TIRES },
  { "rear_tires",      RD_CAR_AXLE, RD_CAR_LIVE,  AX, RD_FLD_TIRES,         "a CarDynamics.Tires name", 0, R, TIRES },
  { "tire_pressure",   RD_CAR_FIELD, RD_CAR_LIVE, CD, RD_FLD_TIRE_PRESSURE_ON, "0 / 1: pressure physics", 0, 0, NULL },
  { "front_tire_pressure", RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_TIRES_PRESSURE, "bar", 0, F, NULL },
  { "rear_tire_pressure",  RD_CAR_AXLE, RD_CAR_LIVE, AX, RD_FLD_TIRES_PRESSURE, "bar", 0, R, NULL },
  /* brakes */
  { "front_brake_torque",     RD_CAR_AXLE,  RD_CAR_LIVE, AX, RD_FLD_BRAKE_TORQUE,      "Nm", 0, F, NULL },
  { "rear_brake_torque",      RD_CAR_AXLE,  RD_CAR_LIVE, AX, RD_FLD_BRAKE_TORQUE,      "Nm", 0, R, NULL },
  { "front_handbrake_torque", RD_CAR_AXLE,  RD_CAR_LIVE, AX, RD_FLD_HANDBRAKE_TORQUE,  "Nm", 0, F, NULL },
  { "rear_handbrake_torque",  RD_CAR_AXLE,  RD_CAR_LIVE, AX, RD_FLD_HANDBRAKE_TORQUE,  "Nm", 0, R, NULL },
  { "brake_balance",          RD_CAR_FIELD, RD_CAR_LIVE, CD, RD_FLD_BRAKE_BALANCE,     "front share", 0, 0, NULL },
  { "handbrake_balance",      RD_CAR_FIELD, RD_CAR_LIVE, CD, RD_FLD_HANDBRAKE_BALANCE, "front share", 0, 0, NULL },
  /* assists */
  { "tcs",          RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_TCS,          "0 / 1", 0, 0, NULL },
  { "tcs_slip",     RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_TCS_SLIP,     "slip allowed", 0, 0, NULL },
  { "esp",          RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_ESP,          "0 / 1", 0, 0, NULL },
  { "esp_strength", RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_ESP_STRENGTH, "strength", 0, 0, NULL },
  { "abs",          RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_ABS,          "0 / 1", 0, 0, NULL },
  { "abs_slip",     RD_CAR_FIELD, RD_CAR_LIVE, CC, RD_FLD_ABS_SLIP,     "slip allowed", 0, 0, NULL },
};

const rd_car_prop *rd_car_props(int *n) {
  *n = (int)(sizeof PROPS / sizeof PROPS[0]);
  return PROPS;
}

const rd_car_prop *rd_car_prop_find(const char *key) {
  for (size_t i = 0; i < sizeof PROPS / sizeof PROPS[0]; i++)
    if (!strcmp(PROPS[i].key, key)) return &PROPS[i];
  return NULL;
}

int rd_car_is(const char *car, const char *name) {
  if (!strcmp(car, RD_CAR_ALL)) return 1;
  size_t n = strlen(name);
  for (int again = 1; again;) {   /* "E30M_Garage(Clone)": strip each suffix in turn */
    again = 0;
    for (const char *const *s = SUFFIXES; *s; s++) {
      const size_t k = strlen(*s);
      if (n > k && !strncmp(name + n - k, *s, k)) { n -= k; again = 1; }
    }
  }
  return strlen(car) == n && !strncmp(car, name, n);
}

int rd_car_enum(const char *const *names, const char *name) {
  for (int i = 0; names && names[i]; i++)
    if (!strcasecmp(names[i], name)) return i;
  return -1;
}

/* 1 if c ends a curve pair: the end of the text or a separator. */
static int pair_end(char c) { return !c || c == ' ' || c == '\t' || c == ','; }

int rd_car_curve_parse(const char *text, float *rpm, float *nm, int max) {
  int n = 0;
  for (const char *p = text; *p;) {
    while (*p && pair_end(*p)) p++;
    if (!*p) break;
    char *end;
    const float r = strtof(p, &end);
    if (end == p || *end != ':' || n == max) return -1;
    p = end + 1;
    const float t = strtof(p, &end);
    if (end == p || !pair_end(*end) || !isfinite(r) || !isfinite(t) || r < 0 ||
        (n && r <= rpm[n - 1]))
      return -1;
    rpm[n] = r;
    nm[n++] = t;
    p = end;
  }
  return n >= 2 ? n : -1;
}

float rd_car_multiplier(float target, float peak) {
  if (!(target > 0) || !(peak > 0)) return -1;   /* also NaN */
  return target / peak;
}

/* Splits "Class.field" (or "field") of key into v; 0, or -1 if a part is empty or too long. */
static int split_key(const char *key, rd_car_value *v) {
  const char *dot = strrchr(key, '.');
  const char *field = dot ? dot + 1 : key;
  if (!*field || strlen(field) >= sizeof v->field) return -1;
  if (dot) {
    const size_t cn = (size_t)(dot - key);
    if (!cn || cn >= sizeof v->cls) return -1;
    memcpy(v->cls, key, cn);
    v->cls[cn] = 0;
  }
  strcpy(v->field, field);
  return 0;
}

/* Checks the value of one of the table's keys; 0, or -1 with the reason in err. */
static int check_value(const rd_car_prop *prop, const rd_ini_entry *e, char *err, size_t errn) {
  float rpm[RD_CAR_CURVE_MAX], nm[RD_CAR_CURVE_MAX];
  if (prop->names && rd_car_enum(prop->names, e->value) < 0) {
    snprintf(err, errn, "cars.ini line %d: %s = \"%s\" is not one of %s", e->line, prop->key,
             e->value, prop->unit);
    return -1;
  }
  if (prop->kind == RD_CAR_CURVE && rd_car_curve_parse(e->value, rpm, nm, RD_CAR_CURVE_MAX) < 0) {
    snprintf(err, errn, "cars.ini line %d: torque_curve needs 2 to %d \"rpm:Nm\" pairs, rpm rising",
             e->line, RD_CAR_CURVE_MAX);
    return -1;
  }
  return 0;
}

int rd_cars_read(const rd_ini *ini, rd_car_value *out, int max, char *err, size_t errn) {
  int n = 0;
  for (int i = 0; i < ini->n && n < max; i++) {
    const rd_ini_entry *e = &ini->e[i];
    rd_car_value *v = &out[n];
    memset(v, 0, sizeof *v);
    if (!e->section[0]) {
      snprintf(err, errn, "cars.ini line %d: \"%s\" is not under a [car] section", e->line, e->key);
      return -1;
    }
    v->prop = rd_car_prop_find(e->key);
    if (v->prop) {
      snprintf(v->cls, sizeof v->cls, "%s", v->prop->cls);
      snprintf(v->field, sizeof v->field, "%s", v->prop->field);
      if (check_value(v->prop, e, err, errn)) return -1;
    } else if (split_key(e->key, v)) {
      snprintf(err, errn, "cars.ini line %d: \"%s\" is not a cars.ini key, field or Class.field",
               e->line, e->key);
      return -1;
    }
    strcpy(v->car, e->section);
    strcpy(v->value, e->value);
    v->line = e->line;
    n++;
  }
  return n;
}
