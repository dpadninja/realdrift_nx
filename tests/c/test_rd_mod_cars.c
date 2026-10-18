/* sources: source/mods/rd_mod_cars.c source/mods/rd_mod_ini.c */
/* Host unit test of rd_mod_cars: cars.ini reading and which objects are a car.
 * MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_mod_cars.h"

static rd_ini g_ini;
static rd_car_value g_v[8];   /* written by rd_cars_read */

/* A section names a car object by its prefab's name or the race copy "(Clone)" -- not the garage
 * or menu models that share the prefix, and not another car whose name starts the same. */
static void test_car_is(void) {
  CHECK(rd_car_is("Skyline", "Skyline") && rd_car_is("Skyline", "Skyline(Clone)"));
  CHECK(!rd_car_is("Skyline", "Skyline_Garage") && !rd_car_is("Skyline", "Skyline_cover"));
  CHECK(!rd_car_is("RS", "RS500") && !rd_car_is("Skyline", "skyline"));
  CHECK(rd_car_is(RD_CAR_ALL, "anything") && rd_car_is(RD_CAR_ALL, ""));
}

/* Plain fields and Class.field keys under car sections are read with car, class, field,
 * value and line; [*] is every car. */
static void test_read(void) {
  char err[160];
  CHECK(rd_ini_parse("[*]\nTCS = 0\n[Skyline]\nESPStrength = 0.3\nDrivetrain.maxPower = 400\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == 3);
  CHECK(!strcmp(g_v[0].car, "*") && !strcmp(g_v[0].cls, "") && !strcmp(g_v[0].field, "TCS"));
  CHECK(!strcmp(g_v[1].car, "Skyline") && !strcmp(g_v[1].field, "ESPStrength") && !strcmp(g_v[1].value, "0.3"));
  CHECK(!strcmp(g_v[2].cls, "Drivetrain") && !strcmp(g_v[2].field, "maxPower") && g_v[2].line == 5);
  CHECK(rd_cars_read(&g_ini, g_v, 2, err, sizeof err) == 2);    /* capped at max */
}

/* cars.ini's own keys map to their class and field (so the port and mod_check find them like a
 * written Class.field) and keep their row; a field name stays a field. */
static void test_own_keys(void) {
  char err[160];
  CHECK(rd_ini_parse("[GTR]\npower = 600\ndrive = awd\nweight = 1300\nturbo = 1\nmaxRPM = 9000\n"
                     "gear_3 = 1.2\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == 6);
  CHECK(g_v[0].prop && g_v[0].prop->kind == RD_CAR_POWER && !strcmp(g_v[0].cls, "Drivetrain"));
  CHECK(!strcmp(g_v[0].field, "powerMultiplier") && !strcmp(g_v[0].value, "600"));
  CHECK(g_v[1].prop->kind == RD_CAR_DRIVE && !strcmp(g_v[1].field, "transmission"));
  CHECK(g_v[2].prop->kind == RD_CAR_BASE && !strcmp(g_v[2].cls, "Vehicle"));
  CHECK(!strcmp(g_v[2].field, "originalWeight"));
  CHECK(g_v[5].prop->kind == RD_CAR_GEAR && g_v[5].prop->index == 3);
  CHECK(g_v[3].prop->kind == RD_CAR_FIELD && !strcmp(g_v[3].field, "turboCharged"));
  CHECK(!g_v[4].prop && !strcmp(g_v[4].field, "maxRPM") && !strcmp(g_v[4].cls, ""));
  CHECK(rd_ini_parse("[GTR]\ndrive = 4WD\n", &g_ini) == 0);   /* not a game drive name */
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "RWD FWD AWD XWD"));
}

/* Every key of the table is lower case, unique, and has a class, field and unit: the docs and
 * mod_check list them from here. */
static void test_table(void) {
  int n;
  const rd_car_prop *t = rd_car_props(&n);
  CHECK(n >= 30);
  for (int i = 0; i < n; i++) {
    CHECK(t[i].cls[0] && t[i].field[0] && t[i].unit[0]);
    CHECK((t[i].kind == RD_CAR_GEAR) == (t[i].index > 0));
    for (const char *c = t[i].key; *c; c++)
      CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
    for (int j = 0; j < i; j++) CHECK(strcmp(t[i].key, t[j].key));
    CHECK(rd_car_prop_find(t[i].key) == &t[i]);
  }
  CHECK(!rd_car_prop_find("TCS") && !rd_car_prop_find(""));
}

/* A Vehicle record is a car's when the last part of its prefab path, its id or its name is the
 * section's car name (which of them the game uses is logged on the console). */
static void test_vehicle_is(void) {
  CHECK(rd_car_vehicle_is("Skyline", "Cars/Skyline", "car_7", "Nissan"));
  CHECK(rd_car_vehicle_is("Skyline", "Skyline", "", "") && rd_car_vehicle_is("car_7", "x/y", "car_7", ""));
  CHECK(rd_car_vehicle_is("Nissan", "x", "y", "Nissan") && rd_car_vehicle_is(RD_CAR_ALL, "", "", ""));
  CHECK(!rd_car_vehicle_is("Skyline", "Cars/Skyline_Garage", "Sky", "line"));
}

/* Drive names are the game's Transmissions in order, any case. */
static void test_drive(void) {
  CHECK(rd_car_drive("RWD") == 0 && rd_car_drive("fwd") == 1 && rd_car_drive("AWD") == 2);
  CHECK(rd_car_drive("XWD") == 3 && rd_car_drive("4x4") == -1 && rd_car_drive("") == -1);
}

/* The power multiplier: the target over the peak the tuning already gives; nothing when the
 * curve has no peak yet (0 before the game computed it) or a value is not positive or NaN. */
static void test_multiplier(void) {
  CHECK(rd_car_multiplier(400, 200, 1) == 2.0f);
  CHECK(rd_car_multiplier(300, 200, 1.5f) == 1.0f);   /* the game's tuning counts */
  CHECK(rd_car_multiplier(400, 0, 1) < 0 && rd_car_multiplier(0, 200, 1) < 0);
  CHECK(rd_car_multiplier(400, 200, 0) < 0 && rd_car_multiplier(0.0f / 0.0f, 200, 1) < 0);
}

/* A value outside a section, an empty field or class, are refused with the line. */
static void test_errors(void) {
  char err[160];
  CHECK(rd_ini_parse("TCS = 0\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "line 1"));
  CHECK(rd_ini_parse("[Skyline]\nDrivetrain. = 1\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "Class.field"));
  CHECK(rd_ini_parse("[Skyline]\n.maxPower = 1\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1);
}

int main(void) {
  RUN(test_car_is);
  RUN(test_read);
  RUN(test_errors);
  RUN(test_own_keys);
  RUN(test_table);
  RUN(test_drive);
  RUN(test_vehicle_is);
  RUN(test_multiplier);
  return g_failures != 0;
}
