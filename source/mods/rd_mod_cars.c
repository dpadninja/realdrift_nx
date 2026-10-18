/* rd_mod_cars.c -- see rd_mod_cars.h. MIT (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "rd_game_symbols.h"
#include "rd_mod_cars.h"

#define CLONE_SUFFIX "(Clone)"   /* Unity's name for an instantiated copy of a prefab */

/* Where each characteristic lives and why it is written so: analysis/research/14-modding.md. */
static const rd_car_prop PROPS[] = {
  /* the engine and gearbox, at once */
  { "power",           RD_CAR_POWER,     RD_CLS_DRIVETRAIN, RD_FLD_POWER_MULTIPLIER, "hp, peak", 0 },
  { "torque",          RD_CAR_TORQUE,    RD_CLS_DRIVETRAIN, RD_FLD_POWER_MULTIPLIER, "Nm, peak", 0 },
  { "drive",           RD_CAR_DRIVE,     RD_CLS_DRIVETRAIN, RD_FLD_TRANSMISSION,     "RWD FWD AWD XWD", 0 },
  { "turbo",           RD_CAR_FIELD,     RD_CLS_DRIVETRAIN, RD_FLD_TURBO,            "0 / 1", 0 },
  { "turbo_bar",       RD_CAR_TURBO_BAR, RD_CLS_DRIVETRAIN, RD_FLD_TURBO_BAR,        "bar", 0 },
  { "rev_limit",       RD_CAR_FIELD,     RD_CLS_DRIVETRAIN, RD_FLD_REV_LIMIT,        "rpm", 0 },
  { "max_rpm",         RD_CAR_FIELD,     RD_CLS_DRIVETRAIN, RD_FLD_MAX_RPM,          "rpm", 0 },
  { "idle_rpm",        RD_CAR_FIELD,     RD_CLS_DRIVETRAIN, RD_FLD_IDLE_RPM,         "rpm", 0 },
  { "final_ratio",     RD_CAR_FIELD,     RD_CLS_DRIVETRAIN, RD_FLD_FINAL_RATIO,      "ratio", 0 },
  { "gear_1",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 1 },
  { "gear_2",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 2 },
  { "gear_3",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 3 },
  { "gear_4",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 4 },
  { "gear_5",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 5 },
  { "gear_6",          RD_CAR_GEAR,      RD_CLS_DRIVETRAIN, RD_FLD_GEAR_RATIOS,      "ratio", 6 },
  /* brakes and assists, at once */
  { "brake_torque",    RD_CAR_FIELD,     RD_CLS_WHEEL,      RD_FLD_BRAKE_TORQUE,     "Nm per wheel", 0 },
  { "handbrake_torque", RD_CAR_FIELD,    RD_CLS_WHEEL,      RD_FLD_HANDBRAKE_TORQUE, "Nm per wheel", 0 },
  { "tcs",             RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_TCS,              "0 / 1", 0 },
  { "tcs_slip",        RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_TCS_SLIP,         "slip allowed", 0 },
  { "esp",             RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_ESP,              "0 / 1", 0 },
  { "esp_strength",    RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_ESP_STRENGTH,     "strength", 0 },
  { "abs",             RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_ABS,              "0 / 1", 0 },
  { "abs_slip",        RD_CAR_FIELD,     RD_CLS_CAR_BASE,   RD_FLD_ABS_SLIP,         "slip allowed", 0 },
  /* the base data the game's tuning starts from, when the car is next built */
  { "weight",          RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_WEIGHT,        "kg", 0 },
  { "shift_time",      RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_SHIFT_TIME,    "s", 0 },
  { "susp_height",     RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_SUSP_HEIGHT,   "game value", 0 },
  { "front_camber",    RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_FRONT_CAMBER,  "game value", 0 },
  { "rear_camber",     RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_REAR_CAMBER,   "game value", 0 },
  { "steering_angle",  RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_STEERING_ANGLE, "degrees", 0 },
  { "weight_distr",    RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_WEIGHT_DISTR,  "game value", 0 },
  { "front_grip",      RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_FRONT_FORWARD_GRIP, "factor", 0 },
  { "front_side_grip", RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_FRONT_SIDEWAYS_GRIP, "factor", 0 },
  { "rear_grip",       RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_REAR_FORWARD_GRIP, "factor", 0 },
  { "rear_side_grip",  RD_CAR_BASE,      RD_CLS_VEHICLE,    RD_FLD_BASE_REAR_SIDEWAYS_GRIP, "factor", 0 },
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
  const size_t n = strlen(car);
  return !strncmp(car, name, n) && (name[n] == 0 || !strcmp(name + n, CLONE_SUFFIX));
}

int rd_car_drive(const char *name) {
  static const char *const DRIVES[] = RD_DRIVES;
  for (size_t i = 0; i < sizeof DRIVES / sizeof DRIVES[0]; i++)
    if (!strcasecmp(DRIVES[i], name)) return (int)i;
  return -1;
}

int rd_car_vehicle_is(const char *car, const char *prefab, const char *id, const char *name) {
  if (!strcmp(car, RD_CAR_ALL)) return 1;
  const char *slash = strrchr(prefab, '/');
  return !strcmp(car, slash ? slash + 1 : prefab) || !strcmp(car, id) || !strcmp(car, name);
}

float rd_car_multiplier(float target, float peak, float tuning) {
  if (!(target > 0) || !(peak > 0) || !(tuning > 0)) return -1;   /* also NaN */
  return target / (peak * tuning);
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
      if (v->prop->kind == RD_CAR_DRIVE && rd_car_drive(e->value) < 0) {
        snprintf(err, errn, "cars.ini line %d: drive = \"%s\" is not one of %s", e->line,
                 e->value, v->prop->unit);
        return -1;
      }
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
