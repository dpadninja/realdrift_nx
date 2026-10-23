/* rd_readlog.c -- see rd_readlog.h. MIT (see LICENSE).
 *
 * File: "RDRL", u32 version, u32 stamp length, the stamp (padded to 8), u32 range count, u32 0,
 * u64 data length, then per range u64 key, i64 offset, u32 length, u32 data offset, then the
 * data. Native byte order: the file is written and read by the same loader on the console. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_readlog.h"

#define MAGIC      "RDRL"
#define VERSION    1u
#define ENTRY      24u                  /* bytes per packed range */
#define PAD8(x)    (((x) + 7u) & ~(size_t)7u)

int rd_readlog_stamp(char *buf, size_t n, int64_t apk_size, int64_t apk_mtime, int64_t obb_size,
                     int64_t obb_mtime) {
  const int k = snprintf(buf, n, "base.apk %lld %lld; obb %lld %lld", (long long)apk_size,
                         (long long)apk_mtime, (long long)obb_size, (long long)obb_mtime);
  return k < 0 || (size_t)k >= n ? -1 : k;
}

void rd_readlog_init(rd_readlog *r, rd_readlog_range *ranges, int cap, uint8_t *data,
                     size_t data_cap) {
  *r = (rd_readlog){ .ranges = ranges, .cap = cap, .data = data, .data_cap = data_cap };
}

int rd_readlog_add(rd_readlog *r, uint64_t key, int64_t off, const void *buf, size_t n) {
  if (n == 0 || off < 0) return 1;
  if (r->n == r->cap || n > r->data_cap - r->used || n > UINT32_MAX) return 0;
  r->ranges[r->n++] = (rd_readlog_range){ .key = key, .off = off, .len = (uint32_t)n,
                                          .data_at = (uint32_t)r->used };
  memcpy(r->data + r->used, buf, n);
  r->used += n;
  return 1;
}

static int by_key_off(const void *a, const void *b) {
  const rd_readlog_range *x = a, *y = b;
  if (x->key != y->key) return x->key < y->key ? -1 : 1;
  if (x->off != y->off) return x->off < y->off ? -1 : 1;
  return x->len > y->len ? -1 : x->len < y->len;        /* the longer first */
}

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t get64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* Walks the sorted ranges merging overlapping / touching ones; writes the packed entries at
 * `entries` and their bytes at `data` unless NULL. Returns the merged count, *bytes the data. */
static int merge(const rd_readlog *r, uint8_t *entries, uint8_t *data, size_t *bytes) {
  int n = 0;
  size_t at = 0, start_at = 0;
  uint64_t key = 0;
  int64_t start = 0, end = -1;
  for (int i = 0; i <= r->n; i++) {
    const rd_readlog_range *g = i < r->n ? &r->ranges[i] : NULL;
    if (g && end >= 0 && g->key == key && g->off <= end) {   /* extends the current range */
      const int64_t gend = g->off + g->len;
      if (gend > end) {
        const size_t skip = (size_t)(end - g->off), k = (size_t)(gend - end);
        if (data) memcpy(data + at, r->data + g->data_at + skip, k);
        at += k;
        end = gend;
      }
      continue;
    }
    if (end >= 0) {                                          /* close the current range */
      if (entries) {
        uint8_t *e = entries + (size_t)n * ENTRY;
        put64(e, key);
        put64(e + 8, (uint64_t)start);
        put32(e + 16, (uint32_t)(end - start));
        put32(e + 20, (uint32_t)start_at);
      }
      n++;
    }
    if (!g) break;
    key = g->key;
    start = g->off;
    end = g->off + g->len;
    start_at = at;
    if (data) memcpy(data + at, r->data + g->data_at, g->len);
    at += g->len;
  }
  *bytes = at;
  return n;
}

size_t rd_readlog_serialize(rd_readlog *r, const char *stamp, uint8_t *out, size_t cap) {
  qsort(r->ranges, (size_t)r->n, sizeof *r->ranges, by_key_off);
  size_t bytes = 0;
  const int n = merge(r, NULL, NULL, &bytes);
  const size_t slen = strlen(stamp);
  const size_t head = PAD8(12 + slen) + 16;
  const size_t total = head + (size_t)n * ENTRY + bytes;
  if (!out) return total;
  if (total > cap) return 0;
  memset(out, 0, head);
  memcpy(out, MAGIC, 4);
  put32(out + 4, VERSION);
  put32(out + 8, (uint32_t)slen);
  memcpy(out + 12, stamp, slen);
  put32(out + head - 16, (uint32_t)n);
  put64(out + head - 8, bytes);
  merge(r, out + head, out + head + (size_t)n * ENTRY, &bytes);
  return total;
}

int rd_readlog_parse(rd_readlog_view *v, const uint8_t *buf, size_t len, const char *stamp) {
  if (!buf || len < 12 || memcmp(buf, MAGIC, 4) || get32(buf + 4) != VERSION) return 0;
  const size_t slen = get32(buf + 8);
  if (slen != strlen(stamp) || len < 12 + slen || memcmp(buf + 12, stamp, slen)) return 0;
  const size_t head = PAD8(12 + slen) + 16;
  if (len < head) return 0;
  const uint32_t n = get32(buf + head - 16);
  const uint64_t bytes = get64(buf + head - 8);
  if (n > (len - head) / ENTRY || bytes != len - head - (size_t)n * ENTRY) return 0;
  const uint8_t *e = buf + head;
  for (uint32_t i = 0; i < n; i++) {                       /* every range inside the data */
    const uint64_t at = get32(e + i * ENTRY + 20), l = get32(e + i * ENTRY + 16);
    if (at + l > bytes) return 0;
  }
  *v = (rd_readlog_view){ .ranges = e, .n = (int)n, .data = e + (size_t)n * ENTRY };
  return 1;
}

const uint8_t *rd_readlog_find(const rd_readlog_view *v, uint64_t key, int64_t off, size_t n) {
  int lo = 0, hi = v->n - 1, best = -1;                    /* last range starting at or before */
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const uint8_t *e = v->ranges + (size_t)mid * ENTRY;
    const uint64_t k = get64(e);
    const int64_t o = (int64_t)get64(e + 8);
    if (k < key || (k == key && o <= off)) { best = mid; lo = mid + 1; }
    else hi = mid - 1;
  }
  if (best < 0) return NULL;
  const uint8_t *e = v->ranges + (size_t)best * ENTRY;
  const int64_t o = (int64_t)get64(e + 8);
  const uint32_t l = get32(e + 16);
  if (get64(e) != key || off < o || off + (int64_t)n > o + l) return NULL;
  return v->data + get32(e + 20) + (size_t)(off - o);
}
