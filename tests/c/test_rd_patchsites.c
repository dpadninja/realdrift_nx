/* sources: source/rd_patchsites.c source/rd_sig.c source/rd_a64.c */
#include <string.h>
#include "check.h"
#include "rd_patchsites.h"

/* A synthetic libunity fragment shaped like the real Choreographer gate (5.0.7 words).
 * Code starts at vaddr 0x1000; the message lives at 0x1800. */
#define BASE 0x1000u
static uint32_t g_code[64];
static char g_msg[64] = "Choreographer available: Enabling VSYNC timing";

static const char *cstr(void *ctx, uint64_t va) { (void)ctx; return va == 0x1800 ? g_msg : NULL; }

static size_t build(size_t at) {
  const uint32_t w[] = {
    0xd10083ff, 0xa9017bf3, 0x94000000, 0x7100401f,
    0x5400000b | (((0x1000 + 4 * (at + 16) - (0x1000 + 4 * (at + 4))) / 4) << 5),  /* b.lt -> at+16 */
    0x910023e0, 0x910023f3, 0x94000000, 0xb2400260, 0x321a03e1, 0x94000000, 0x94000000,
    0x90000000,                                   /* adrp x0, page(0x1000) */
    0x91000000 | (0x800u << 10),                  /* add x0, x0, #0x800 -> 0x1800 */
    0xd503201f, 0xd503201f, 0xa9417bf3,           /* ... ldp x19, x30 (epilogue) at +16 */
  };
  memcpy(&g_code[at], w, sizeof w);
  return at;
}

static rd_image image(void) {
  rd_image im = { g_code, BASE, 64, cstr, NULL };
  return im;
}

static void test_found(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  rd_image im = image(); uint64_t gate = 0, skip = 0;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_OK);
  CHECK(gate == BASE + 4 * (4 + 3));
  CHECK(skip == BASE + 4 * (4 + 16));
}

static void test_wrong_string_rejected(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  strcpy(g_msg, "Something else");
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_BAD_CHECK);
  strcpy(g_msg, "Choreographer available: Enabling VSYNC timing");
}

static void test_branch_must_reach_epilogue(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  g_code[4 + 16] = 0xd503201f;                  /* no ldp x19,x30 at the b.lt target */
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_BAD_CHECK);
}

static void test_ambiguous_and_missing(void) {
  memset(g_code, 0, sizeof g_code); build(0); build(30);
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_AMBIGUOUS);
  memset(g_code, 0, sizeof g_code);
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_NOT_FOUND);
  rd_gc_vars v;
  CHECK(rd_find_gc_vars(&im, &v) == RD_SITE_NOT_FOUND);
}

int main(void) {
  RUN(test_found);
  RUN(test_wrong_string_rejected);
  RUN(test_branch_must_reach_epilogue);
  RUN(test_ambiguous_and_missing);
  return g_failures != 0;
}
