/* rd_etc2bc.h -- ETC1/ETC2 texture data re-encoded as S3TC (BC1 / BC3), block by block.
 *
 * The Switch's GPU has no ETC2, so mesa decompresses every ETC2 texture to RGBA8: 8x the size
 * of ETC2 RGB, 4x of RGBA, and the game's textures then fill the newlib heap until new ones
 * fail and sample black (analysis/research/25). The GPU has S3TC, whose 4x4 blocks have the
 * same size as ETC2's (8 bytes for RGB, 16 with alpha), so a texture can be re-encoded in place:
 * same dimensions, offsets and data size, only the format changes. Each block is decoded with
 * etcdec (iOrange) and encoded with stb_dxt (ryg / stb), both vendored here.
 * Pure logic, host-tested (tests/c/test_rd_etc2bc.c); the GL side is rd_etc2bc_gl.c.
 * Thread-safe: no shared state. MIT (see LICENSE). */
#ifndef RD_ETC2BC_H
#define RD_ETC2BC_H

#include <stdint.h>

/* The S3TC format ETC format `fmt` is re-encoded as, or 0 when it is not handled (punchthrough
 * alpha and EAC R11 / RG11 stay with mesa: their blocks do not map onto a same-size S3TC one):
 *   ETC1, ETC2 RGB8 -> DXT1; ETC2 SRGB8 -> SRGB DXT1; ETC2 RGBA8 EAC -> DXT5;
 *   ETC2 SRGB8 ALPHA8 EAC -> SRGB ALPHA DXT5. */
uint32_t rd_etc2bc_target(uint32_t fmt);

/* Bytes per 4x4 block of a handled format (8 or 16), 0 for others. */
int rd_etc2bc_block_bytes(uint32_t fmt);

/* Blocks across and down a w x h image (partial blocks count). */
void rd_etc2bc_blocks(int w, int h, int *blocks_w, int *blocks_h);

/* Re-encodes block rows [row0, row1) of an image `blocks_w` blocks wide from `src` (format fmt,
 * rows packed) into `dst` (its rd_etc2bc_target format, same layout). src and dst do not
 * overlap. Workers call it for disjoint row ranges of the same image. */
void rd_etc2bc_rows(uint32_t fmt, const uint8_t *src, uint8_t *dst, int blocks_w, int row0,
                    int row1);

/* Rows [*lo, *hi) of part i when `total` rows are shared by `parts` parts as evenly as
 * possible (empty when there are more parts than rows). */
void rd_etc2bc_split(int total, int parts, int i, int *lo, int *hi);

#endif
