/* rd_mod_cars.h -- cars.ini: a car's characteristics by car name, without classes or patterns.
 * Host-testable (tests/c/test_rd_mod_cars.c); rd_mods_patches.c writes the values into the game.
 *
 *   [Skyline]                  a car, by the game's name (or [*] for every car)
 *   power = 420                a key of the table (rd_car_props): one of the game's own values,
 *   drive = AWD                in its own units and names
 *   ESPStrength = 0.3          or a field of one of the car's main components (RD_CAR_COMPONENTS)
 *   Drivetrain.maxRPM = 9000   or Class.field for any component of the car
 *
 * A car object is a GameObject carrying a Drivetrain: the race car "X(Clone)", the garage model
 * "X_Garage(Clone)", the menu model "X_cover"; a section [X] matches its name without the game's
 * suffixes (RD_CAR_NAME_SUFFIXES). A component belongs to the car object it is on or under.
 * Where each key lives and when the game reads it: analysis/research/14-modding.md.
 * MIT (see LICENSE). */
#ifndef RD_MOD_CARS_H
#define RD_MOD_CARS_H

#include "rd_mod_ini.h"

#define RD_CAR_ALL        "*"    /* the section for every car */
#define RD_CAR_PARENTS    12     /* how far up the object tree a component's car is looked for */
#define RD_CAR_CURVE_MAX  32     /* keys of a torque_curve */
#define RD_CAR_GEARS_MAX  10     /* forward gears of `gears` */

/* How a cars.ini key reaches the game. */
typedef enum {
  RD_CAR_FIELD,      /* a field of a car component: cls.field = value */
  RD_CAR_AXLE,       /* a field of the front and/or rear Axle (CarDynamics.axles) -- a name if
                        `names` -- then the game's SetWheelsParams / SetBrakes / SetTiresType */
  RD_CAR_DRIVE,      /* Drivetrain.transmission by name, then SetTransmission */
  RD_CAR_POWER,      /* stock peak hp: powerMultiplier from the game's CalcMaxPowerMaxTorque */
  RD_CAR_CURVE,      /* Drivetrain.torqueCurve: rpm:Nm keys through AnimationCurve.SetKeys */
  RD_CAR_TURBO_BAR,  /* maxTurboPressure, and the limit Awake derives from it */
  RD_CAR_GEARS,      /* the forward gears: Drivetrain.gearRatios from `first` on, replaced by
                        a new array of that many gears (the reverse and neutral kept) */
  RD_CAR_MASS,       /* kg: the car's Rigidbody.mass through its setter */
} rd_car_kind;

/* When a key is written, matching when the game reads it. */
typedef enum {
  RD_CAR_LIVE,       /* read while driving: every pass (each second), only when it differs */
  RD_CAR_ONCE,       /* never rewritten by the game, costly: when a car is built, on scene change */
  RD_CAR_BUILD,      /* the car's originals: right before the game reads them (garage model
                        loaded, race car built), so the player's tuning applies on top */
} rd_car_when;

#define RD_AXLE_FRONT 1
#define RD_AXLE_REAR  2

/* One of cars.ini's own keys. */
typedef struct {
  const char *key;            /* lower case, as written in cars.ini */
  rd_car_kind kind;
  rd_car_when when;
  const char *cls;            /* the class written to (RD_CAR_AXLE: Axle) */
  const char *field;          /* its field */
  const char *unit;           /* what the value is, for docs and checks */
  int axle;                   /* RD_CAR_AXLE: RD_AXLE_FRONT | RD_AXLE_REAR */
  const char *const *names;   /* an enum field: its value names in order, NULL-terminated */
} rd_car_prop;

/* The table of cars.ini keys, n entries into *n. */
const rd_car_prop *rd_car_props(int *n);

/* The table row for key, or NULL if key is a field name instead. */
const rd_car_prop *rd_car_prop_find(const char *key);

/* One cars.ini value. */
typedef struct {
  char car[RD_INI_SECTION_LEN];  /* a car name, or RD_CAR_ALL */
  char cls[RD_INI_KEY_LEN];      /* "" = find the field in RD_CAR_COMPONENTS */
  char field[RD_INI_KEY_LEN];
  char value[RD_INI_VALUE_LEN];
  int  line;
  const rd_car_prop *prop;       /* the key's row, NULL for a field */
} rd_car_value;

/* The values of a parsed cars.ini, at most max. Returns how many, or -1 with a reason in err
 * (a value outside a [car] section, an empty field or class name, a name the game's enum does
 * not have, a bad torque_curve). */
int rd_cars_read(const rd_ini *ini, rd_car_value *out, int max, char *err, size_t errn);

/* 1 if a car object called `name` is car `car` of a section: the name without the game's
 * suffixes ("(Clone)", "_Garage", "_cover", ...) is the car name; RD_CAR_ALL matches all. */
int rd_car_is(const char *car, const char *name);

/* The index of `name` (any case) in a NULL-terminated name list, or -1. */
int rd_car_enum(const char *const *names, const char *name);

/* A torque_curve value "rpm:Nm rpm:Nm ..." into rpm[] / nm[] (at most max pairs). Returns the
 * number of pairs, or -1 unless there are 2 or more, all finite, rpm >= 0 and rising. */
int rd_car_curve_parse(const char *text, float *rpm, float *nm, int max);

/* A gears value "3.6 2.1 1.5 ..." (ratios of 1st, 2nd, ...) into out (at most max). Returns
 * the number of gears, or -1 unless there are 1 or more, all finite and above 0. */
int rd_car_gears_parse(const char *text, float *out, int max);

/* The powerMultiplier giving peak `target` when the curve's own peak (at factor 1) is `peak`;
 * -1 when it cannot be computed (no peak, bad value). */
float rd_car_multiplier(float target, float peak);

#endif
