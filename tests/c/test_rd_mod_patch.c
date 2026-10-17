/* sources: source/mods/rd_mod_patch.c */
/* Host unit test of rd_mod_patch: patch.ini values by field type. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_il2cpp.h"
#include "rd_mod_patch.h"

/* Each supported type parses its usual spellings and rejects junk, out-of-range values and
 * NaN, so a typo in patch.ini never writes garbage into a game object. */
static void test_parse(void) {
  rd_patch_value v;
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_BOOLEAN, "true", &v) == 0 && v.v.b == 1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_BOOLEAN, "0", &v) == 0 && v.v.b == 0);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_BOOLEAN, "yes", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_I4, "-12", &v) == 0 && v.v.i32 == -12);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_I4, "0x10", &v) == 0 && v.v.i32 == 16);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_I4, "3000000000", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_I4, "12abc", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_U4, "-1", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_U4, "4000000000", &v) == 0 && v.v.u32 == 4000000000u);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_I8, "-9000000000", &v) == 0 && v.v.i64 == -9000000000LL);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_R4, "9500.5", &v) == 0 && v.v.f32 == 9500.5f);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_R8, "1e-3", &v) == 0 && v.v.f64 == 1e-3);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_R4, "nan", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_R4, "", &v) == -1);
  CHECK(rd_patch_parse(RD_IL2CPP_TYPE_R4, NULL, &v) == -1);
  CHECK(rd_patch_parse(0x0e /* string */, "x", &v) == -2);
}

/* A value is written at its offset with its own width and nothing next to it changes (a
 * wrong width would corrupt the neighbouring field). */
static void test_write(void) {
  unsigned char obj[32];
  memset(obj, 0xAA, sizeof obj);
  rd_patch_value v;
  rd_patch_parse(RD_IL2CPP_TYPE_R4, "2.5", &v);
  rd_patch_write(obj, 8, &v);
  float f;
  memcpy(&f, obj + 8, 4);
  CHECK(f == 2.5f && obj[7] == 0xAA && obj[12] == 0xAA);
  rd_patch_parse(RD_IL2CPP_TYPE_BOOLEAN, "1", &v);
  rd_patch_write(obj, 20, &v);
  CHECK(obj[20] == 1 && obj[21] == 0xAA);
  rd_patch_parse(RD_IL2CPP_TYPE_I8, "-2", &v);
  rd_patch_write(obj, 0, &v);
  long long ll;
  memcpy(&ll, obj, 8);
  CHECK(ll == -2 && obj[8] != 0xFF);
  rd_patch_value bad = { .type = 0x0e };
  rd_patch_write(obj, 24, &bad);   /* unsupported: nothing written */
  CHECK(obj[24] == 0xAA);
}

/* Type names for log lines. */
static void test_type_names(void) {
  CHECK(!strcmp(rd_patch_type_name(RD_IL2CPP_TYPE_R4), "float"));
  CHECK(!strcmp(rd_patch_type_name(RD_IL2CPP_TYPE_U4), "uint"));
  CHECK(!strcmp(rd_patch_type_name(0x0e), "unsupported"));
}

int main(void) {
  RUN(test_parse);
  RUN(test_write);
  RUN(test_type_names);
  return g_failures != 0;
}
