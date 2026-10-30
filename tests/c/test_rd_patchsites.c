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

/* QualitySettings::get_antiAliasing, 5.0.8 words (0x2ed034). */
static const uint32_t GET_AA[] = { 0xf81f0ffe, 0x94057bb0, 0xb9804c08, 0xf9401809, 0x5280140a,
                                   0x9b0a2508, 0xb9406500, 0xf84107fe, 0xd65f03c0 };

/* The getter yields the manager call and the offsets it reads: current level at 0x4c, levels
 * at 0x30, 0xa0 bytes per level, antiAliasing at 0x64. */
static void test_quality_aa(void) {
  rd_quality_aa q = { 0 };
  CHECK(rd_find_quality_aa(GET_AA, 9, &q) == RD_SITE_OK);
  CHECK(q.call == 1 && q.current == 0x4c && q.levels == 0x30 && q.stride == 0xa0 &&
        q.field == 0x64);
}

/* Other code, a cut-off body or a zero stride is not taken for the getter. */
static void test_quality_aa_refuses_other_code(void) {
  rd_quality_aa q;
  uint32_t c[9];
  CHECK(rd_find_quality_aa(GET_AA, 8, &q) == RD_SITE_NOT_FOUND);
  memcpy(c, GET_AA, sizeof c);
  c[6] = 0xb9406501;                                /* ldr w1, [x8, #0x64]: not the result */
  CHECK(rd_find_quality_aa(c, 9, &q) == RD_SITE_NOT_FOUND);
  memcpy(c, GET_AA, sizeof c);
  c[4] = 0x5280000a;                                /* mov w10, #0 */
  CHECK(rd_find_quality_aa(c, 9, &q) == RD_SITE_BAD_CHECK);
}

/* ---- il2cpp copy and splash flag: synthetic fragments (5.0.8 words) at BASE ---- */
static uint32_t g_big[1024];
static const char *g_at_1800, *g_at_1900;

static const char *cstr2(void *ctx, uint64_t va) {
  (void)ctx;
  return va == 0x1800 ? g_at_1800 : va == 0x1900 ? g_at_1900 : NULL;
}

static rd_image image2(size_t nwords) {
  rd_image im = { g_big, BASE, nwords, cstr2, NULL };
  return im;
}

static uint32_t bl_to(size_t from, size_t to) {
  return 0x94000000u | ((uint32_t)(to - from) & 0x03ffffffu);
}

/* The il2cpp copy call at word 2 (CopyDirectory at word 20, message at 0x1800). */
static void build_copy(void) {
  memset(g_big, 0, sizeof g_big);
  const uint32_t w[] = { 0x910103e0, 0x9111e3e1, 0x320003e2, bl_to(5, 20), 0x370002a0,
                         0xf944abe8, 0xf9445be9, 0x90000000, 0x91000000 | (0x800u << 10) };
  memcpy(&g_big[2], w, sizeof w);
  g_big[20] = 0xa9ba6ffc;
  g_at_1800 = "  il2cpp failed to extract resources (%s => %s)";
}

/* The copy call, its target and the padding right after the code are found; the message, the
 * callee's prologue and room for the veneer in the last page are all required. */
static void test_il2cpp_copy(void) {
  build_copy();
  rd_image im = image2(32);
  uint64_t call = 0, copy = 0, veneer = 0;
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_OK);
  CHECK(call == BASE + 4 * 5 && copy == BASE + 4 * 20 && veneer == BASE + 4 * 32);
  g_at_1800 = "something else";
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_BAD_CHECK);
  build_copy();
  g_big[20] = 0xd503201f;
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_BAD_CHECK);
  build_copy();
  im = image2(1022);                                  /* code ends 8 bytes before the page */
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_BAD_CHECK);
  im = image2(32);
  im.cstr = NULL;
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_BAD_CHECK);
  memset(g_big, 0, sizeof g_big);
  im = image2(32);
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_NOT_FOUND);
  build_copy();
  memcpy(&g_big[40], &g_big[2], 9 * sizeof g_big[0]);
  im = image2(64);
  CHECK(rd_find_il2cpp_copy(&im, &call, &copy, &veneer) == RD_SITE_AMBIGUOUS);
}

/* SplashSettings::Transfer at word 10 naming the field at +8 (word 14, name at 0x1900), called
 * by PlayerSettings::Transfer at word 30 with the struct at +0x138. */
static void build_splash(void) {
  memset(g_big, 0, sizeof g_big);
  g_big[10] = 0xd101c3ff;                             /* sub sp, sp, #0x70 */
  const uint32_t name[] = { 0x90000001, 0x91002283, 0x91240021 };
  memcpy(&g_big[14], name, sizeof name);
  const uint32_t call[] = { 0x9104e280, 0xaa1303e1, bl_to(32, 10) };
  memcpy(&g_big[30], call, sizeof call);
  g_at_1900 = "m_ShowUnitySplashScreen";
}

/* The offset is the struct's plus the field's; the name must be the splash flag, its function
 * must be the callee (no return in between) and each must be unique. */
static void test_splash_flag(void) {
  build_splash();
  rd_image im = image2(64);
  uint32_t field = 0;
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_OK && field == 0x140);
  g_at_1900 = "m_ShowUnitySplashLogo";
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_NOT_FOUND);
  build_splash();
  memcpy(&g_big[50], &g_big[14], 3 * sizeof g_big[0]);  /* a second reference to the name */
  g_big[51] = 0x91002283;
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_AMBIGUOUS);
  build_splash();
  g_big[12] = 0xd65f03c0;                             /* ret: the callee ends before the name */
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_NOT_FOUND);
  build_splash();
  memcpy(&g_big[40], &g_big[30], 3 * sizeof g_big[0]);
  g_big[42] = bl_to(42, 10);                          /* two callers */
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_AMBIGUOUS);
  build_splash();
  g_big[30] = 0x91000280;                             /* add x0, x20, #0: no struct offset */
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_BAD_CHECK);
  im.cstr = NULL;
  CHECK(rd_find_splash_flag(&im, &field) == RD_SITE_BAD_CHECK);
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
  RUN(test_quality_aa);
  RUN(test_quality_aa_refuses_other_code);
  RUN(test_il2cpp_copy);
  RUN(test_splash_flag);
  return g_failures != 0;
}
