/* rd_etc2bc.c -- see rd_etc2bc.h. The only file that compiles the vendored codecs.
 * MIT (see LICENSE). */
#include <stddef.h>
#include <string.h>   /* stb_dxt: memcpy, memset */
#include "rd_etc2bc.h"

#define ETCDEC_STATIC
#define ETCDEC_IMPLEMENTATION
#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "third_party/etcdec.h"
#include "third_party/stb_dxt.h"
#pragma GCC diagnostic pop

/* GL format codes (GLES 3.0, OES_compressed_ETC1_RGB8_texture, EXT_texture_compression_s3tc,
 * EXT_texture_compression_s3tc_srgb). */
#define ETC1_RGB8            0x8D64u
#define ETC2_RGB8            0x9274u
#define ETC2_SRGB8           0x9275u
#define ETC2_RGBA8_EAC       0x9278u
#define ETC2_SRGB8_ALPHA8    0x9279u
#define DXT1_RGB             0x83F0u
#define DXT5_RGBA            0x83F3u
#define DXT1_SRGB            0x8C4Cu
#define DXT5_SRGB_ALPHA      0x8C4Fu

uint32_t rd_etc2bc_target(uint32_t fmt) {
  switch (fmt) {
    case ETC1_RGB8: case ETC2_RGB8: return DXT1_RGB;
    case ETC2_SRGB8:                return DXT1_SRGB;
    case ETC2_RGBA8_EAC:            return DXT5_RGBA;
    case ETC2_SRGB8_ALPHA8:         return DXT5_SRGB_ALPHA;
    default:                        return 0;
  }
}

/* 1 for the formats with an EAC alpha half (-> DXT5). */
static int has_alpha(uint32_t fmt) {
  return fmt == ETC2_RGBA8_EAC || fmt == ETC2_SRGB8_ALPHA8;
}

int rd_etc2bc_block_bytes(uint32_t fmt) {
  if (!rd_etc2bc_target(fmt)) return 0;
  return has_alpha(fmt) ? 16 : 8;
}

void rd_etc2bc_blocks(int w, int h, int *blocks_w, int *blocks_h) {
  *blocks_w = w > 0 ? (w + 3) / 4 : 0;
  *blocks_h = h > 0 ? (h + 3) / 4 : 0;
}

void rd_etc2bc_rows(uint32_t fmt, const uint8_t *src, uint8_t *dst, int blocks_w, int row0,
                    int row1) {
  const int alpha = has_alpha(fmt);
  const size_t bb = alpha ? 16 : 8;
  unsigned char rgba[4 * 4 * 4];
  for (int row = row0; row < row1; row++) {
    const size_t first = (size_t)row * (size_t)blocks_w * bb;
    for (int x = 0; x < blocks_w; x++) {
      const size_t off = first + (size_t)x * bb;
      if (alpha) etcdec_eac_rgba(src + off, rgba, 4 * 4);
      else       etcdec_etc_rgb(src + off, rgba, 4 * 4);
      stb_compress_dxt_block(dst + off, rgba, alpha, STB_DXT_NORMAL);
    }
  }
}

void rd_etc2bc_split(int total, int parts, int i, int *lo, int *hi) {
  if (parts <= 0 || i < 0 || i >= parts || total <= 0) { *lo = *hi = 0; return; }
  *lo = (int)((long long)total * i / parts);
  *hi = (int)((long long)total * (i + 1) / parts);
}
