/* rd_zipview.c -- see rd_zipview.h. MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include "rd_zipfmt.h"
#include "rd_zipview.h"

/* Extra field that pads a local header so the entry's data is aligned (the id Android's
 * zipalign uses): id, length, then the alignment and zero bytes. */
#define ZIP_ALIGN_EXTRA_ID   0xD935u
#define ZIP_ALIGN_EXTRA_MIN  6   /* id + length + alignment value */

/* Reads exactly n bytes of the original at off; 0 or -1. */
static int read_orig(rd_zipview_read_fn read, void *ctx, uint64_t off, void *buf, size_t n) {
  return read(ctx, -1, off, buf, n) == (long)n ? 0 : -1;
}

typedef struct { uint64_t cd_off, cd_size; uint32_t entries; } eocd;

/* Finds and parses the end-of-central-directory record in the archive's last bytes; 0, -1 if
 * there is none, -2 for ZIP64. */
static int find_eocd(uint64_t size, rd_zipview_read_fn read, void *ctx, eocd *e) {
  if (size < ZIP_EOCD_SIZE) return -1;
  const size_t win = size < ZIP_EOCD_SIZE + ZIP_EOCD_MAX_COMMENT
                         ? (size_t)size : ZIP_EOCD_SIZE + ZIP_EOCD_MAX_COMMENT;
  uint8_t *buf = malloc(win);
  if (!buf || read_orig(read, ctx, size - win, buf, win)) { free(buf); return -1; }
  int r = -1;
  for (size_t i = win - ZIP_EOCD_SIZE + 1; i-- > 0;) {
    if (zip_le32(buf + i) != ZIP_EOCD_SIG) continue;
    e->entries = zip_le16(buf + i + ZIP_EOCD_ENTRIES);
    e->cd_size = zip_le32(buf + i + ZIP_EOCD_CD_SIZE);
    e->cd_off = zip_le32(buf + i + ZIP_EOCD_CD_OFFSET);
    if (e->entries == ZIP_U16_MAX || e->cd_size == ZIP_U32_MAX || e->cd_off == ZIP_U32_MAX)
      r = -2;
    else
      r = e->cd_off + e->cd_size <= size - win + i ? 0 : -1;
    break;
  }
  free(buf);
  return r;
}

/* Length of the central directory record at p (0 if it is not one or runs past end). */
static size_t cd_record_len(const uint8_t *p, const uint8_t *end) {
  if (end - p < ZIP_CDH_SIZE || zip_le32(p) != ZIP_CDH_SIG) return 0;
  const size_t len = ZIP_CDH_SIZE + (size_t)zip_le16(p + ZIP_CDH_NAME_LEN) +
                     zip_le16(p + ZIP_CDH_EXTRA_LEN) + zip_le16(p + ZIP_CDH_COMMENT_LEN);
  return (size_t)(end - p) >= len ? len : 0;
}

/* Padding (in the alignment extra field) that aligns data following a header ending at `at`. */
static size_t align_pad(uint64_t at) {
  size_t pad = (size_t)((RD_ZIPVIEW_ALIGN - at % RD_ZIPVIEW_ALIGN) % RD_ZIPVIEW_ALIGN);
  if (pad && pad < ZIP_ALIGN_EXTRA_MIN) pad += RD_ZIPVIEW_ALIGN;   /* too short for the field */
  return pad;
}

/* Writes the local header of `f` at p (time/date from `tmpl`, a central record, or zero). */
static size_t put_local(uint8_t *p, const rd_zipview_file *f, const uint8_t *tmpl, size_t pad) {
  const size_t nlen = strlen(f->name);
  memset(p, 0, ZIP_LFH_SIZE + nlen + pad);
  zip_put32(p, ZIP_LFH_SIG);
  zip_put16(p + ZIP_LFH_VERSION_NEED, ZIP_VERSION_STORED);
  if (tmpl) memcpy(p + ZIP_LFH_TIME, tmpl + ZIP_CDH_TIME, 4);
  zip_put32(p + ZIP_LFH_CRC, f->crc);
  zip_put32(p + ZIP_LFH_COMP_SIZE, (uint32_t)f->size);
  zip_put32(p + ZIP_LFH_SIZE_UNCOMP, (uint32_t)f->size);
  zip_put16(p + ZIP_LFH_NAME_LEN, (uint32_t)nlen);
  zip_put16(p + ZIP_LFH_EXTRA_LEN, (uint32_t)pad);
  memcpy(p + ZIP_LFH_SIZE, f->name, nlen);
  if (pad) {
    uint8_t *x = p + ZIP_LFH_SIZE + nlen;
    zip_put16(x, ZIP_ALIGN_EXTRA_ID);
    zip_put16(x + 2, (uint32_t)(pad - 4));
    zip_put16(x + 4, RD_ZIPVIEW_ALIGN);
  }
  return ZIP_LFH_SIZE + nlen + pad;
}

/* Writes the central record of `f` (stored, at local offset lho) at p, keeping the version
 * made-by and time/date of the original record `tmpl` when it replaces one. */
static size_t put_central(uint8_t *p, const rd_zipview_file *f, const uint8_t *tmpl, uint64_t lho) {
  const size_t nlen = strlen(f->name);
  memset(p, 0, ZIP_CDH_SIZE);
  if (tmpl) memcpy(p, tmpl, ZIP_CDH_SIZE);   /* made-by, time/date, attributes */
  zip_put32(p, ZIP_CDH_SIG);
  zip_put16(p + ZIP_CDH_VERSION_NEED, ZIP_VERSION_STORED);
  zip_put16(p + ZIP_CDH_FLAGS, 0);             /* no data descriptor, no encryption */
  zip_put16(p + ZIP_CDH_METHOD, ZIP_STORED);
  zip_put32(p + ZIP_CDH_CRC, f->crc);
  zip_put32(p + ZIP_CDH_COMP_SIZE, (uint32_t)f->size);
  zip_put32(p + ZIP_CDH_SIZE_UNCOMP, (uint32_t)f->size);
  zip_put16(p + ZIP_CDH_NAME_LEN, (uint32_t)nlen);
  zip_put16(p + ZIP_CDH_EXTRA_LEN, 0);
  zip_put16(p + ZIP_CDH_COMMENT_LEN, 0);
  zip_put32(p + ZIP_CDH_LOCAL_OFF, (uint32_t)lho);
  memcpy(p + ZIP_CDH_SIZE, f->name, nlen);
  return ZIP_CDH_SIZE + nlen;
}

/* Index of the file named like the central record at rec, or -1. */
static int match(const uint8_t *rec, const rd_zipview_file *files, const int *use, int n) {
  const size_t nlen = zip_le16(rec + ZIP_CDH_NAME_LEN);
  for (int i = 0; i < n; i++)
    if (use[i] && strlen(files[i].name) == nlen && !memcmp(rec + ZIP_CDH_SIZE, files[i].name, nlen))
      return i;
  return -1;
}

/* Appends a segment. */
static void seg(rd_zipview *v, int kind, int src, uint64_t len, uint64_t off) {
  if (!len) return;
  v->segs[v->nsegs++] = (rd_zipview_seg){ kind, src, v->size, len, off };
  v->size += len;
}

int rd_zipview_build(rd_zipview *v, uint64_t orig_size, const rd_zipview_file *files, int n,
                     rd_zipview_read_fn read, void *ctx) {
  memset(v, 0, sizeof *v);
  eocd e;
  int r = find_eocd(orig_size, read, ctx, &e);
  if (r) return r;
  uint8_t *cd = malloc(e.cd_size ? e.cd_size : 1);
  int *use = calloc((size_t)n + 1, sizeof *use), *pads = calloc((size_t)n + 1, sizeof *pads);
  uint64_t *lho = calloc((size_t)n + 1, sizeof *lho);
  const uint8_t **tmpl = calloc((size_t)n + 1, sizeof *tmpl);
  if (!cd || !use || !pads || !lho || !tmpl) { r = -3; goto out; }
  if (read_orig(read, ctx, e.cd_off, cd, e.cd_size)) { r = -1; goto out; }

  /* Which files are used (first of a name wins) and which original record each replaces. */
  for (int i = 0; i < n; i++) {
    use[i] = files[i].name && files[i].name[0] && files[i].size < ZIP_U32_MAX;
    for (int j = 0; j < i && use[i]; j++) use[i] = !use[j] || strcmp(files[i].name, files[j].name);
  }
  const uint8_t *end = cd + e.cd_size;
  size_t new_cd = 0;
  uint32_t entries = 0;
  for (const uint8_t *p = cd; p < end; entries++) {
    const size_t len = cd_record_len(p, end);
    if (!len) { r = -1; goto out; }
    const int i = match(p, files, use, n);
    if (i >= 0 && !tmpl[i]) { tmpl[i] = p; v->replaced++; }
    new_cd += i >= 0 ? ZIP_CDH_SIZE + strlen(files[i].name) : len;
    p += len;
  }

  /* Appended entries start where the original directory was. */
  uint64_t at = e.cd_off;
  size_t local_bytes = 0;
  for (int i = 0; i < n; i++) {
    if (!use[i]) continue;
    const size_t hdr = ZIP_LFH_SIZE + strlen(files[i].name);
    lho[i] = at;
    const size_t pad = align_pad(at + hdr);
    pads[i] = (int)pad;
    local_bytes += hdr + pad;
    at += hdr + pad + files[i].size;
    if (!tmpl[i]) { v->added++; entries++; new_cd += ZIP_CDH_SIZE + strlen(files[i].name); }
  }
  if (at + new_cd + ZIP_EOCD_SIZE > ZIP_U32_MAX || entries >= ZIP_U16_MAX) { r = -2; goto out; }

  v->mem = malloc(local_bytes + new_cd + ZIP_EOCD_SIZE);
  v->segs = calloc((size_t)n * 2 + 2, sizeof *v->segs);
  if (!v->mem || !v->segs) { r = -3; goto out; }
  seg(v, RD_ZV_ORIG, -1, e.cd_off, 0);
  size_t m = 0;
  for (int i = 0; i < n; i++) {
    if (!use[i]) continue;
    const size_t hl = put_local(v->mem + m, &files[i], tmpl[i], (size_t)pads[i]);
    seg(v, RD_ZV_MEM, -1, hl, m);
    m += hl;
    seg(v, RD_ZV_SRC, files[i].src, files[i].size, 0);
  }
  const size_t cd_start = m;
  for (const uint8_t *p = cd; p < end;) {
    const size_t len = cd_record_len(p, end);
    const int i = match(p, files, use, n);
    if (i >= 0 && tmpl[i] == p) m += put_central(v->mem + m, &files[i], p, lho[i]);
    else if (i < 0) { memcpy(v->mem + m, p, len); m += len; }
    p += len;
  }
  for (int i = 0; i < n; i++)
    if (use[i] && !tmpl[i]) m += put_central(v->mem + m, &files[i], NULL, lho[i]);
  uint8_t *eo = v->mem + m;
  memset(eo, 0, ZIP_EOCD_SIZE);
  zip_put32(eo, ZIP_EOCD_SIG);
  zip_put16(eo + ZIP_EOCD_DISK_ENTRIES, entries);
  zip_put16(eo + ZIP_EOCD_ENTRIES, entries);
  zip_put32(eo + ZIP_EOCD_CD_SIZE, (uint32_t)(m - cd_start));
  zip_put32(eo + ZIP_EOCD_CD_OFFSET, (uint32_t)at);
  m += ZIP_EOCD_SIZE;
  seg(v, RD_ZV_MEM, -1, m - cd_start, cd_start);
  r = 0;
out:
  free(cd); free(use); free(pads); free(lho); free(tmpl);
  if (r) rd_zipview_free(v);
  return r;
}

long rd_zipview_read(const rd_zipview *v, uint64_t off, void *buf, size_t n,
                     rd_zipview_read_fn read, void *ctx) {
  if (off >= v->size) return 0;
  if (n > v->size - off) n = (size_t)(v->size - off);
  /* the last segment starting at or before off */
  int lo = 0, hi = v->nsegs - 1;
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    if (v->segs[mid].voff <= off) lo = mid; else hi = mid - 1;
  }
  size_t done = 0;
  for (int i = lo; i < v->nsegs && done < n; i++) {
    const rd_zipview_seg *s = &v->segs[i];
    const uint64_t in = off + done - s->voff;
    const size_t take = (size_t)(s->len - in) < n - done ? (size_t)(s->len - in) : n - done;
    uint8_t *dst = (uint8_t *)buf + done;
    if (s->kind == RD_ZV_MEM) {
      memcpy(dst, v->mem + s->off + in, take);
    } else {
      const long got = read(ctx, s->kind == RD_ZV_SRC ? s->src : -1, s->off + in, dst, take);
      if (got < 0) return -1;
      /* a mod file that shrank after the view was built reads as zeros, never past `take` */
      if ((size_t)got < take) memset(dst + got, 0, take - (size_t)got);
    }
    done += take;
  }
  return (long)done;
}

void rd_zipview_free(rd_zipview *v) {
  free(v->segs);
  free(v->mem);
  memset(v, 0, sizeof *v);
}
