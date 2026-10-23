/* sources: source/android/rd_etc2bc.c */
/* Host unit test of rd_etc2bc (ETC2 -> S3TC re-encoding). MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rd_etc2bc.h"

/* An ETC1 / ETC2 "individual" block of one colour: both halves 4-bit (r, g, b), table 0 and
 * every pixel index 0 (modifier +2), so each pixel decodes to c * 17 + 2. */
static void etc_flat(uint8_t *b, int r, int g, int bl) {
  b[0] = (uint8_t)(r << 4 | r); b[1] = (uint8_t)(g << 4 | g); b[2] = (uint8_t)(bl << 4 | bl);
  memset(b + 3, 0, 5);
}

/* An EAC alpha half of one value: base `a`, multiplier 0. */
static void eac_flat(uint8_t *b, int a) {
  b[0] = (uint8_t)a;
  memset(b + 1, 0, 7);
}

/* RGB of a 5:6:5 colour, expanded to 8 bits. */
static void rgb565(unsigned c, int *r, int *g, int *b) {
  *r = (int)((c >> 11) & 31) * 255 / 31;
  *g = (int)((c >> 5) & 63) * 255 / 63;
  *b = (int)(c & 31) * 255 / 31;
}

/* Colour of pixel p (0..15) of a BC1 colour block: the endpoints, or a mix of them. */
static void bc1_pixel(const uint8_t *b, int p, int *r, int *g, int *bl) {
  const unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
  int r0, g0, b0, r1, g1, b1;
  rgb565(c0, &r0, &g0, &b0);
  rgb565(c1, &r1, &g1, &b1);
  const int idx = (b[4 + p / 4] >> (2 * (p % 4))) & 3;
  const int w4[4][2] = { { 3, 0 }, { 0, 3 }, { 2, 1 }, { 1, 2 } };   /* thirds, c0 > c1 */
  const int w3[4][2] = { { 2, 0 }, { 0, 2 }, { 1, 1 }, { 0, 0 } };   /* halves, else */
  const int (*w)[2] = c0 > c1 ? w4 : w3;
  const int d = c0 > c1 ? 3 : 2;
  *r = (r0 * w[idx][0] + r1 * w[idx][1]) / d;
  *g = (g0 * w[idx][0] + g1 * w[idx][1]) / d;
  *bl = (b0 * w[idx][0] + b1 * w[idx][1]) / d;
}

/* Alpha of pixel p of a BC3 alpha block. */
static int bc3_alpha(const uint8_t *b, int p) {
  uint64_t bits = 0;
  for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
  const int idx = (int)((bits >> (3 * p)) & 7), a0 = b[0], a1 = b[1];
  if (idx == 0) return a0;
  if (idx == 1) return a1;
  if (a0 > a1) return (a0 * (8 - idx) + a1 * (idx - 1)) / 7;
  if (idx == 6) return 0;
  if (idx == 7) return 255;
  return (a0 * (6 - idx) + a1 * (idx - 1)) / 5;
}

static int near(int a, int b) { return abs(a - b) <= 8; }

/* Every pixel of BC1 block `b` has colour (r, g, bl), within the 5:6:5 rounding. */
static int bc1_is(const uint8_t *b, int r, int g, int bl) {
  for (int p = 0; p < 16; p++) {
    int pr, pg, pb;
    bc1_pixel(b, p, &pr, &pg, &pb);
    if (!near(pr, r) || !near(pg, g) || !near(pb, bl)) return 0;
  }
  return 1;
}

/* ETC formats map onto the same-size S3TC ones; what has no such block is left to mesa. */
static void test_formats(void) {
  CHECK(rd_etc2bc_target(0x8D64) == 0x83F0 && rd_etc2bc_target(0x9274) == 0x83F0);
  CHECK(rd_etc2bc_target(0x9275) == 0x8C4C && rd_etc2bc_target(0x9278) == 0x83F3);
  CHECK(rd_etc2bc_target(0x9279) == 0x8C4F);
  CHECK(rd_etc2bc_target(0x9276) == 0 && rd_etc2bc_target(0x9270) == 0);   /* A1, EAC R11 */
  CHECK(rd_etc2bc_target(0x8058) == 0);                                     /* RGBA8 */
  CHECK(rd_etc2bc_block_bytes(0x9274) == 8 && rd_etc2bc_block_bytes(0x9278) == 16);
  CHECK(rd_etc2bc_block_bytes(0x8058) == 0);
  int bw, bh;
  rd_etc2bc_blocks(1024, 2, &bw, &bh);
  CHECK(bw == 256 && bh == 1);
  rd_etc2bc_blocks(1, 1, &bw, &bh);                                         /* the last mip */
  CHECK(bw == 1 && bh == 1);
  rd_etc2bc_blocks(0, 4, &bw, &bh);
  CHECK(bw == 0 && bh == 1);
}

/* A 3 x 2 block ETC2 RGB image of six flat colours comes out as BC1 blocks of the same
 * colours in the same places, also when the rows are done as separate ranges (workers). */
static void test_rgb_image(void) {
  const int col[6][3] = { { 8, 4, 12 }, { 0, 0, 0 }, { 15, 15, 15 }, { 2, 9, 5 }, { 13, 1, 7 }, { 6, 6, 3 } };
  uint8_t src[6 * 8], dst[6 * 8];
  for (int i = 0; i < 6; i++) etc_flat(src + 8 * i, col[i][0], col[i][1], col[i][2]);
  memset(dst, 0xAA, sizeof dst);
  rd_etc2bc_rows(0x9274, src, dst, 3, 1, 2);
  rd_etc2bc_rows(0x9274, src, dst, 3, 0, 1);
  for (int i = 0; i < 6; i++) {
    const int r = col[i][0] * 17 + 2, g = col[i][1] * 17 + 2, b = col[i][2] * 17 + 2;
    CHECK(bc1_is(dst + 8 * i, r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b));
  }
}

/* ETC2 RGBA8 EAC -> BC3: the alpha half keeps its value, the colour half its colour, each in
 * its own 8 bytes of the 16-byte block. */
static void test_rgba_block(void) {
  uint8_t src[16], dst[16];
  eac_flat(src, 200);
  etc_flat(src + 8, 3, 10, 14);
  rd_etc2bc_rows(0x9278, src, dst, 1, 0, 1);
  for (int p = 0; p < 16; p++) CHECK(near(bc3_alpha(dst, p), 200));
  CHECK(bc1_is(dst + 8, 3 * 17 + 2, 10 * 17 + 2, 14 * 17 + 2));
}

/* Row ranges of the parts cover every row once, in order, and parts beyond the rows are
 * empty; bad arguments give an empty range. */
static void test_split(void) {
  const int totals[] = { 1, 5, 12, 256, 7 };
  for (unsigned t = 0; t < sizeof totals / sizeof totals[0]; t++)
    for (int parts = 1; parts <= 13; parts++) {
      int next = 0;
      for (int i = 0; i < parts; i++) {
        int lo, hi;
        rd_etc2bc_split(totals[t], parts, i, &lo, &hi);
        CHECK(lo == next && hi >= lo);
        next = hi;
      }
      CHECK(next == totals[t]);
    }
  int lo = 1, hi = 1;
  rd_etc2bc_split(4, 0, 0, &lo, &hi);
  CHECK(lo == 0 && hi == 0);
  rd_etc2bc_split(4, 2, 2, &lo, &hi);
  CHECK(lo == 0 && hi == 0);
}

int main(void) {
  RUN(test_formats);
  RUN(test_rgb_image);
  RUN(test_rgba_block);
  RUN(test_split);
  return g_failures != 0;
}
