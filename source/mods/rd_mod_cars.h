/* rd_mod_cars.h -- cars.ini: a car's characteristics by car name, without classes or patterns.
 * Host-testable (tests/c/test_rd_mod_cars.c); rd_mods_patches.c turns the values into patches.
 *
 *   [Skyline]                  a car, by the game's name (or [*] for every car)
 *   power = 420                a key of the table below (rd_car_props): the characteristic,
 *   drive = AWD                in the player's units, written the way the game uses it
 *   ESPStrength = 0.3          or a field of one of the car's main components (RD_CAR_COMPONENTS)
 *   Drivetrain.maxRPM = 9000   or Class.field for any component of the car
 *
 * A car is a GameObject carrying a CarController, named "X" or "X(Clone)" (the race copy of
 * the prefab); a component belongs to it when it is on that object or under it (the wheels).
 * The garage and menu models ("X_Garage", "X_cover") carry no CarController and are left alone.
 * MIT (see LICENSE). */
#ifndef RD_MOD_CARS_H
#define RD_MOD_CARS_H

#include "rd_mod_ini.h"

#define RD_CAR_ALL     "*"    /* the section for every car */
#define RD_CAR_PARENTS 12     /* how far up the object tree a component's car is looked for */

/* How a cars.ini key reaches the game. */
typedef enum {
  RD_CAR_FIELD,      /* a field of a car component, read while driving: cls.field = value */
  RD_CAR_DRIVE,      /* RWD/FWD/AWD/XWD into Drivetrain.transmission, then SetTransmission */
  RD_CAR_POWER,      /* peak hp: powerMultiplier = value / (maxPower * externalMultiplier) */
  RD_CAR_TORQUE,     /* peak Nm: the same through maxTorque */
  RD_CAR_TURBO_BAR,  /* maxTurboPressure, and the limit Awake derives from it */
  RD_CAR_GEAR,       /* Drivetrain.gearRatios[first + index - 1] */
  RD_CAR_BASE,       /* a float of the car's Vehicle record: the base the game's tuning starts
                        from when it builds the car (garage, race start) */
} rd_car_kind;

/* One of cars.ini's own keys. */
typedef struct {
  const char *key;    /* lower case, as written in cars.ini */
  rd_car_kind kind;
  const char *cls;    /* the class written to */
  const char *field;  /* its field */
  const char *unit;   /* what the value is, for docs and checks */
  int index;          /* RD_CAR_GEAR: the gear, 1 = first */
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
 * (a value outside a [car] section, an empty field or class name, a bad drive name). */
int rd_cars_read(const rd_ini *ini, rd_car_value *out, int max, char *err, size_t errn);

/* 1 if a car object called `name` is car `car` of a section: the same name, or the name plus
 * "(Clone)"; RD_CAR_ALL matches every car. */
int rd_car_is(const char *car, const char *name);

/* The Transmissions value of a drive name (RD_DRIVES, any case), or -1. */
int rd_car_drive(const char *name);

/* 1 if a Vehicle record is car `car` of a section: the last part of its prefab path, its id or
 * its name is the car name; RD_CAR_ALL matches every record. */
int rd_car_vehicle_is(const char *car, const char *prefab, const char *id, const char *name);

/* The powerMultiplier giving peak `target` when the curve's own peak is `peak` and the game's
 * tuning multiplies it by `tuning`; -1 when it cannot be computed (no peak yet, bad value). */
float rd_car_multiplier(float target, float peak, float tuning);

#endif
