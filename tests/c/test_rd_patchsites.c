/* sources: source/engine/rd_patchsites.c source/engine/rd_sig.c source/engine/rd_a64.c */
/* Host unit test of rd_patchsites. MIT (see LICENSE). */
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

/* The Choreographer gate pattern finds the b.lt and its skip target in a fragment shaped like
 * the real libunity code. */
static void test_found(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  rd_image im = image(); uint64_t gate = 0, skip = 0;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_OK);
  CHECK(gate == BASE + 4 * (4 + 3));
  CHECK(skip == BASE + 4 * (4 + 16));
}

/* A match whose adrp/add does not point at the Choreographer log string is refused: the cross-
 * check stops a look-alike sequence from being patched. */
static void test_wrong_string_rejected(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  strcpy(g_msg, "Something else");
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_BAD_CHECK);
  strcpy(g_msg, "Choreographer available: Enabling VSYNC timing");
}

/* A match whose b.lt does not land on the function epilogue is refused, so the patch can only
 * ever skip the vsync setup. */
static void test_branch_must_reach_epilogue(void) {
  memset(g_code, 0, sizeof g_code); build(4);
  g_code[4 + 16] = 0xd503201f;                  /* no ldp x19,x30 at the b.lt target */
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_BAD_CHECK);
}

/* Two matches are ambiguous and none is not found (also for the GC variables): the loader then
 * skips the patch instead of guessing. */
static void test_ambiguous_and_missing(void) {
  memset(g_code, 0, sizeof g_code); build(0); build(30);
  rd_image im = image(); uint64_t gate, skip;
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_AMBIGUOUS);
  memset(g_code, 0, sizeof g_code);
  CHECK(rd_find_choreo_gate(&im, &gate, &skip) == RD_SITE_NOT_FOUND);
  rd_gc_vars v;
  CHECK(rd_find_gc_vars(&im, &v) == RD_SITE_NOT_FOUND);
}

/* A synthetic blit-type reader at word 40 calling a GetPlayerSettings at word 50 (5.0.8 words);
 * GetPlayerSettings calls GetManagerFromContext at word 60. */
static void build_blit(void) {
  memset(g_code, 0, sizeof g_code);
  const uint32_t reader[] = { 0x94000000 | 10, 0xb9424008, 0x7100091f, 0x54000520, 0x7100051f, 0x540004a1 };
  memcpy(&g_code[40], reader, sizeof reader);
  const uint32_t get_ps[] = { 0xf81f0ffe, 0x2a1f03e0, 0x94000000 | 8, 0xf84107fe, 0xd65f03c0 };
  memcpy(&g_code[50], get_ps, sizeof get_ps);
}

/* The blit-type reader gives GetPlayerSettings (its call), GetManagerFromContext (the call in
 * GetPlayerSettings) and the field offset (0x240 in the real build). */
static void test_blit_type(void) {
  build_blit();
  rd_image im = image(); uint64_t gs = 0, gm = 0; uint32_t field = 0;
  CHECK(rd_find_blit_type(&im, &gs, &gm, &field) == RD_SITE_OK);
  CHECK(gs == BASE + 4 * 50 && gm == BASE + 4 * 60 && field == 0x240);
}

/* A reader whose call does not land on a GetPlayerSettings-shaped function is refused: the
 * loader replaces that function's body, so it must be the right one. */
static void test_blit_type_checks_callee(void) {
  build_blit();
  g_code[51] = 0x52800020;                         /* mov w0, #1: another context */
  rd_image im = image(); uint64_t gs, gm; uint32_t field;
  CHECK(rd_find_blit_type(&im, &gs, &gm, &field) == RD_SITE_BAD_CHECK);
  memset(g_code, 0, sizeof g_code);
  CHECK(rd_find_blit_type(&im, &gs, &gm, &field) == RD_SITE_NOT_FOUND);
}

/* SleekRender's SetActiveRenderTextureAndClear (libil2cpp 5.0.8 words): the branch goes from the
 * word after the set_active call to the epilogue, skipping only the clear. */
static const uint32_t SET_CLEAR[] = {
  0xd10083ff, 0xa9017bfd, 0x910043fd, 0xaa1f03e1, 0x94000001,   /* frame, bl set_active */
  0xb0000008, 0xbd401903, 0x1e2e1000, 0x1e2d1001, 0x1e2c1002,   /* Color, ...           */
  0x910003e0, 0xaa1f03e1, 0xa9007fff, 0x94000002, 0x2d4007e0,
  0x2d410fe2, 0x32000000, 0x32000001, 0xaa1f03e2, 0x94000003,   /* bl GL.Clear          */
  0xa9417bfd, 0x910083ff, 0xd65f03c0,                           /* epilogue             */
};

/* The real method's shape: skip from right after set_active (word 5) to the epilogue (word 20). */
static void test_skip_clear(void) {
  size_t from = 0, to = 0;
  CHECK(rd_find_skip_clear(SET_CLEAR, 23, &from, &to) == RD_SITE_OK && from == 5 && to == 20);
}

/* Another method shape (no set_active call first, no call before the epilogue, no epilogue)
 * is not patched. */
static void test_skip_clear_refuses_other_code(void) {
  uint32_t c[23];
  size_t from, to;
  memcpy(c, SET_CLEAR, sizeof c);
  c[3] = 0xaa1f03e2;                                /* mov x2, xzr */
  CHECK(rd_find_skip_clear(c, 23, &from, &to) == RD_SITE_NOT_FOUND);
  memcpy(c, SET_CLEAR, sizeof c);
  CHECK(rd_find_skip_clear(c, 20, &from, &to) == RD_SITE_NOT_FOUND);
  const uint32_t no_call[] = { 0xd10083ff, 0xa9017bfd, 0x910043fd, 0xaa1f03e1, 0x94000001,
                               0xd503201f, 0xa9417bfd, 0x910083ff, 0xd65f03c0 };
  CHECK(rd_find_skip_clear(no_call, 9, &from, &to) == RD_SITE_BAD_CHECK);
}

int main(void) {
  RUN(test_found);
  RUN(test_blit_type);
  RUN(test_blit_type_checks_callee);
  RUN(test_skip_clear);
  RUN(test_skip_clear_refuses_other_code);
  RUN(test_wrong_string_rejected);
  RUN(test_branch_must_reach_epilogue);
  RUN(test_ambiguous_and_missing);
  return g_failures != 0;
}
