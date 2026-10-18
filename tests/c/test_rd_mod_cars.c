/* sources: source/mods/rd_mod_cars.c source/mods/rd_mod_ini.c */
/* Host unit test of rd_mod_cars: cars.ini reading, the key table, names, curves.
 * MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_mod_cars.h"

static rd_ini g_ini;
static rd_car_value g_v[8];   /* written by rd_cars_read */

/* A section names a car object by its prefab name, with the game's suffixes stripped: the
 * race copy "(Clone)", the garage model "_Garage(Clone)", the menu model "_cover" -- but not
 * another car whose name starts the same, and case matters. */
static void test_car_is(void) {
  CHECK(rd_car_is("Skyline", "Skyline") && rd_car_is("Skyline", "Skyline(Clone)"));
  CHECK(rd_car_is("Skyline", "Skyline_Garage(Clone)") && rd_car_is("Skyline", "Skyline_cover"));
  CHECK(rd_car_is("E30M", "E30M_Cover") && rd_car_is("E30M", "E30M_Garage"));
  CHECK(!rd_car_is("RS", "RS500") && !rd_car_is("Skyline", "skyline") && !rd_car_is("Sky", "Skyline"));
  CHECK(!rd_car_is("Skyline", "(Clone)") && !rd_car_is("Skyline", "Skyline_Old"));
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

/* The table's keys map to the class and field the game uses, with their kind and timing; a
 * field name stays a field. */
static void test_own_keys(void) {
  char err[160];
  CHECK(rd_ini_parse("[GTR]\npower = 600\ndrive = awd\nweight = 1300\nrear_diff = two_way_LSD\n"
                     "front_spring = 50000\ngears = 3.6 2.1 1.5\nmaxRPM = 9000\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == 7);
  CHECK(g_v[0].prop->kind == RD_CAR_POWER && g_v[0].prop->when == RD_CAR_ONCE);
  CHECK(!strcmp(g_v[0].cls, "Drivetrain") && !strcmp(g_v[0].field, "powerMultiplier"));
  CHECK(g_v[1].prop->kind == RD_CAR_DRIVE && !strcmp(g_v[1].field, "transmission"));
  CHECK(g_v[2].prop->kind == RD_CAR_MASS && g_v[2].prop->when == RD_CAR_BUILD);
  CHECK(g_v[3].prop->kind == RD_CAR_AXLE && g_v[3].prop->axle == RD_AXLE_REAR && !strcmp(g_v[3].cls, "Axle"));
  CHECK(!strcmp(g_v[3].field, "differential") && g_v[3].prop->names);
  CHECK(g_v[4].prop->axle == RD_AXLE_FRONT && g_v[4].prop->when == RD_CAR_LIVE);
  CHECK(g_v[5].prop->kind == RD_CAR_GEARS && g_v[5].prop->when == RD_CAR_BUILD);
  CHECK(!g_v[6].prop && !strcmp(g_v[6].field, "maxRPM") && !strcmp(g_v[6].cls, ""));
}

/* Enum keys accept only the game's own names (any case), and a torque curve must be valid --
 * both are refused at read time with the line, before the game sees them. */
static void test_value_errors(void) {
  char err[200];
  CHECK(rd_ini_parse("[GTR]\ndrive = 4WD\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "RWD FWD AWD XWD"));
  CHECK(rd_ini_parse("[GTR]\nrear_diff = lsd\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "two_way_LSD"));
  CHECK(rd_ini_parse("[GTR]\nfront_tires = slick\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "line 2"));
  CHECK(rd_ini_parse("[GTR]\ntorque_curve = 1000:100\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "torque_curve"));
  CHECK(rd_ini_parse("[GTR]\ngears = 3.6 0 1.5\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == -1 && strstr(err, "gears"));
  CHECK(rd_ini_parse("[GTR]\nfront_tires = SPORT_front\n", &g_ini) == 0);
  CHECK(rd_cars_read(&g_ini, g_v, 8, err, sizeof err) == 1);
}

/* Every key is lower case, unique, has a class, field and unit; axle keys say which axle;
 * enum keys have names. The docs and mod_check list them from here. */
static void test_table(void) {
  int n;
  const rd_car_prop *t = rd_car_props(&n);
  CHECK(n >= 50 && rd_car_prop_find("gears") && !rd_car_prop_find("gear_1"));
  for (int i = 0; i < n; i++) {
    CHECK(t[i].cls[0] && t[i].field[0] && t[i].unit[0]);
    CHECK((t[i].kind == RD_CAR_AXLE) == (t[i].axle != 0));
    CHECK(!t[i].names || t[i].kind == RD_CAR_AXLE || t[i].kind == RD_CAR_DRIVE);
    for (const char *c = t[i].key; *c; c++)
      CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
    for (int j = 0; j < i; j++) CHECK(strcmp(t[i].key, t[j].key));
    CHECK(rd_car_prop_find(t[i].key) == &t[i]);
  }
  CHECK(!rd_car_prop_find("TCS") && !rd_car_prop_find(""));
  CHECK(rd_car_prop_find("susp_height")->axle == (RD_AXLE_FRONT | RD_AXLE_REAR));
}

/* Enum names: the game's order, any case, NULL-terminated lists. */
static void test_enum(void) {
  static const char *const names[] = { "RWD", "FWD", "AWD", "XWD", NULL };
  CHECK(rd_car_enum(names, "RWD") == 0 && rd_car_enum(names, "fwd") == 1 && rd_car_enum(names, "XWD") == 3);
  CHECK(rd_car_enum(names, "4x4") == -1 && rd_car_enum(names, "") == -1 && rd_car_enum(NULL, "RWD") == -1);
}

/* A torque curve is rpm:Nm pairs separated by spaces or commas, rpm rising from 0 or more;
 * negative torque is allowed (engine braking at the top, as the game's own curves have). */
static void test_curve(void) {
  float rpm[RD_CAR_CURVE_MAX], nm[RD_CAR_CURVE_MAX];
  CHECK(rd_car_curve_parse("0:0 4500:240, 7503:-1.7", rpm, nm, RD_CAR_CURVE_MAX) == 3);
  CHECK(rpm[1] == 4500 && nm[1] == 240 && nm[2] < 0);
  CHECK(rd_car_curve_parse("0:0 1000:100 ", rpm, nm, RD_CAR_CURVE_MAX) == 2);
  CHECK(rd_car_curve_parse("1000:100", rpm, nm, RD_CAR_CURVE_MAX) == -1);            /* one pair */
  CHECK(rd_car_curve_parse("0:0 0:10", rpm, nm, RD_CAR_CURVE_MAX) == -1);            /* not rising */
  CHECK(rd_car_curve_parse("-5:0 10:10", rpm, nm, RD_CAR_CURVE_MAX) == -1);
  CHECK(rd_car_curve_parse("0:0 1000x:5", rpm, nm, RD_CAR_CURVE_MAX) == -1);
  CHECK(rd_car_curve_parse("0:0 1000:5nm", rpm, nm, RD_CAR_CURVE_MAX) == -1);
  CHECK(rd_car_curve_parse("0:0 1:1 2:2", rpm, nm, 2) == -1);                         /* too many */
  CHECK(rd_car_curve_parse("", rpm, nm, RD_CAR_CURVE_MAX) == -1);
}

/* Gears: one ratio per forward gear, so their count is the number of gears; each above 0
 * (0 would be a neutral, not a missing gear), at most RD_CAR_GEARS_MAX. */
static void test_gears(void) {
  float g[RD_CAR_GEARS_MAX];
  CHECK(rd_car_gears_parse("3.6 2.1, 1.5 1.15 0.95", g, RD_CAR_GEARS_MAX) == 5 && g[4] == 0.95f);
  CHECK(rd_car_gears_parse("4.0", g, RD_CAR_GEARS_MAX) == 1);
  CHECK(rd_car_gears_parse("3.6 0 1.5", g, RD_CAR_GEARS_MAX) == -1);
  CHECK(rd_car_gears_parse("3.6 -1", g, RD_CAR_GEARS_MAX) == -1);
  CHECK(rd_car_gears_parse("3.6 x", g, RD_CAR_GEARS_MAX) == -1 && rd_car_gears_parse("3.6:1", g, RD_CAR_GEARS_MAX) == -1);
  CHECK(rd_car_gears_parse("1 2 3", g, 2) == -1 && rd_car_gears_parse("", g, RD_CAR_GEARS_MAX) == -1);
}

/* The power multiplier: the target over the curve's own peak; nothing when the peak is not
 * known (0) or a value is not positive or NaN. */
static void test_multiplier(void) {
  CHECK(rd_car_multiplier(400, 200) == 2.0f && rd_car_multiplier(150, 200) == 0.75f);
  CHECK(rd_car_multiplier(400, 0) < 0 && rd_car_multiplier(0, 200) < 0);
  CHECK(rd_car_multiplier(0.0f / 0.0f, 200) < 0);
}

int main(void) {
  RUN(test_car_is);
  RUN(test_read);
  RUN(test_errors);
  RUN(test_own_keys);
  RUN(test_value_errors);
  RUN(test_table);
  RUN(test_enum);
  RUN(test_curve);
  RUN(test_gears);
  RUN(test_multiplier);
  return g_failures != 0;
}
