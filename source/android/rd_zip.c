/* rd_zip.c -- see rd_zip.h. MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "rd_zip.h"

/* ZIP records (PKWARE APPNOTE 4.3): signatures, fixed sizes, field offsets used here. */
#define ZIP_EOCD_SIG        0x06054b50u   /* end of central directory */
#define ZIP_EOCD_SIZE       22
#define ZIP_EOCD_MAX_COMMENT 65535
#define ZIP_EOCD_CD_SIZE    12
#define ZIP_EOCD_CD_OFFSET  16

#define ZIP_CDH_SIG         0x02014b50u   /* central directory file header */
#define ZIP_CDH_SIZE        46
#define ZIP_CDH_METHOD      10
#define ZIP_CDH_COMP_SIZE   20
#define ZIP_CDH_SIZE_UNCOMP 24
#define ZIP_CDH_NAME_LEN    28
#define ZIP_CDH_EXTRA_LEN   30
#define ZIP_CDH_COMMENT_LEN 32
#define ZIP_CDH_LOCAL_OFF   42

#define ZIP_LFH_SIG         0x04034b50u   /* local file header */
#define ZIP_LFH_SIZE        30
#define ZIP_LFH_NAME_LEN    26
#define ZIP_LFH_EXTRA_LEN   28

#define ZIP_STORED          0
#define ZIP_DEFLATED        8

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
/* Little-endian 32-bit value at p (zip headers are little-endian). */
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Reads exactly n bytes at offset off; 0 or -1. */
static int read_at(FILE *f, uint64_t off, void *buf, size_t n) {
  return fseeko(f, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n ? 0 : -1;
}

/* The end-of-central-directory record sits in the last EOCD_SIZE + comment bytes. */
static int find_eocd(FILE *f, uint64_t *cd_off, uint64_t *cd_size) {
  if (fseeko(f, 0, SEEK_END)) return -1;
  off_t end = ftello(f);
  if (end < ZIP_EOCD_SIZE) return -1;
  size_t win = end < ZIP_EOCD_SIZE + ZIP_EOCD_MAX_COMMENT ? (size_t)end
                                                       : ZIP_EOCD_SIZE + ZIP_EOCD_MAX_COMMENT;
  uint8_t *buf = malloc(win);
  if (!buf || read_at(f, (uint64_t)end - win, buf, win)) { free(buf); return -1; }
  int ok = -1;
  for (size_t i = win - ZIP_EOCD_SIZE + 1; i-- > 0;) {
    if (le32(buf + i) == ZIP_EOCD_SIG) {
      *cd_size = le32(buf + i + ZIP_EOCD_CD_SIZE);
      *cd_off = le32(buf + i + ZIP_EOCD_CD_OFFSET);
      ok = 0;
      break;
    }
  }
  free(buf);
  return ok;
}

int rd_zip_find(FILE *f, const char *name, rd_zip_entry *e) {
  uint64_t cd_off, cd_size;
  if (!f || !name || find_eocd(f, &cd_off, &cd_size)) return -1;
  uint8_t *cd = malloc(cd_size ? cd_size : 1);
  if (!cd || read_at(f, cd_off, cd, cd_size)) { free(cd); return -1; }
  size_t nlen = strlen(name);
  int found = -1;
  for (uint64_t p = 0; p + ZIP_CDH_SIZE <= cd_size && le32(cd + p) == ZIP_CDH_SIG;) {
    uint16_t fn = le16(cd + p + ZIP_CDH_NAME_LEN), ex = le16(cd + p + ZIP_CDH_EXTRA_LEN),
             cm = le16(cd + p + ZIP_CDH_COMMENT_LEN);
    if (p + ZIP_CDH_SIZE + fn > cd_size) break;
    if (fn == nlen && !memcmp(cd + p + ZIP_CDH_SIZE, name, nlen)) {
      uint8_t lh[ZIP_LFH_SIZE];
      uint64_t lho = le32(cd + p + ZIP_CDH_LOCAL_OFF);
      if (read_at(f, lho, lh, sizeof lh) || le32(lh) != ZIP_LFH_SIG) break;
      e->method = le16(cd + p + ZIP_CDH_METHOD);
      e->comp_size = le32(cd + p + ZIP_CDH_COMP_SIZE);
      e->size = le32(cd + p + ZIP_CDH_SIZE_UNCOMP);
      e->data_offset = lho + ZIP_LFH_SIZE + le16(lh + ZIP_LFH_NAME_LEN) + le16(lh + ZIP_LFH_EXTRA_LEN);
      found = 0;
      break;
    }
    p += (uint64_t)ZIP_CDH_SIZE + fn + ex + cm;
  }
  free(cd);
  return found;
}

void *rd_zip_read(FILE *f, const rd_zip_entry *e) {
  if (!f || !e || (e->method != ZIP_STORED && e->method != ZIP_DEFLATED)) return NULL;
  uint8_t *out = malloc(e->size + 1);
  uint8_t *in = e->method == ZIP_DEFLATED ? malloc(e->comp_size ? e->comp_size : 1) : out;
  if (!out || !in) { free(out); if (in != out) free(in); return NULL; }
  int ok = read_at(f, e->data_offset, in, e->method == ZIP_DEFLATED ? e->comp_size : e->size) == 0;
  if (ok && e->method == ZIP_DEFLATED) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    ok = inflateInit2(&zs, -MAX_WBITS) == Z_OK;
    if (ok) {
      zs.next_in = in;  zs.avail_in = (uInt)e->comp_size;
      zs.next_out = out; zs.avail_out = (uInt)e->size;
      ok = inflate(&zs, Z_FINISH) == Z_STREAM_END && zs.total_out == e->size;
      inflateEnd(&zs);
    }
    free(in);
  }
  if (!ok) { free(out); return NULL; }
  out[e->size] = 0;
  return out;
}
