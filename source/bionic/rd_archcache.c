/* rd_archcache.c -- see rd_archcache.h. MIT (see LICENSE). */
#include <stdio.h>      /* SEEK_* */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "rd_archcache.h"

void rd_archcache_init(rd_archcache *c, rd_archcache_block *blocks, int nblocks,
                       uint32_t block_bytes, uint32_t sector_bytes) {
  memset(blocks, 0, (size_t)nblocks * sizeof(*blocks));
  *c = (rd_archcache){ .blocks = blocks, .nblocks = nblocks, .block_bytes = block_bytes,
                       .sector_bytes = sector_bytes };
}

/* The block holding (key, index), or NULL. */
static rd_archcache_block *find(rd_archcache *c, uint64_t key, int64_t index) {
  for (int i = 0; i < c->nblocks; i++) {
    rd_archcache_block *b = &c->blocks[i];
    if (b->valid && b->key == key && b->index == index) return b;
  }
  return NULL;
}

/* An unused slot, else the least recently used one, emptied for (key, index); NULL without
 * memory. */
static rd_archcache_block *take(rd_archcache *c, uint64_t key, int64_t index) {
  rd_archcache_block *b = &c->blocks[0];
  for (int i = 0; i < c->nblocks; i++) {
    rd_archcache_block *s = &c->blocks[i];
    if (!s->valid) { b = s; break; }
    if (s->used < b->used) b = s;
  }
  b->valid = 0;
  if (!b->data) b->data = malloc(c->block_bytes);
  if (!b->data) return NULL;
  b->key = key;
  b->index = index;
  return b;
}

/* Reads sectors [s0, s1) of block b (file `size` bytes long) through fill and marks the ones
 * that came in whole, or that end the file. Returns 0, or -1 when fill failed. */
static int fill_sectors(rd_archcache *c, rd_archcache_block *b, int64_t size, int s0, int s1,
                        rd_archcache_fill fill, void *ctx) {
  const int64_t start = b->index * (int64_t)c->block_bytes;
  const int64_t lo = start + (int64_t)s0 * c->sector_bytes;
  int64_t hi = start + (int64_t)s1 * c->sector_bytes;
  if (hi > size) hi = size;
  const long got = fill(ctx, lo, b->data + (lo - start), (size_t)(hi - lo));
  if (got <= 0) return -1;
  for (int s = s0; s < s1; s++) {
    const int64_t end = start + (int64_t)(s + 1) * c->sector_bytes;
    if (lo + got >= (end < size ? end : size)) b->valid |= 1u << s;
  }
  return 0;
}

/* Makes bytes [in, in + k) of block `index` valid: its missing sectors in that range (with
 * `ahead`, through the end of the block) in one fill from the first missing one. Returns the
 * block, or NULL when fill or memory failed. */
static rd_archcache_block *get(rd_archcache *c, uint64_t key, int64_t size, int64_t index,
                               size_t in, size_t k, int ahead, rd_archcache_fill fill,
                               void *ctx) {
  const int per = (int)(c->block_bytes / c->sector_bytes);
  const int64_t start = index * (int64_t)c->block_bytes;
  int last = (int)((size - start + c->sector_bytes - 1) / c->sector_bytes);  /* past the file */
  if (last > per) last = per;
  const int need0 = (int)(in / c->sector_bytes);
  const int need1 = (int)((in + k - 1) / c->sector_bytes) + 1;
  rd_archcache_block *b = find(c, key, index);
  uint32_t want = 0;
  for (int s = need0; s < need1; s++) want |= 1u << s;
  if (b && (b->valid & want) == want) {
    c->hits++;
  } else {
    c->misses++;
    if (!b && !(b = take(c, key, index))) return NULL;
    int s0 = need0;
    while (b->valid & (1u << s0)) s0++;
    int s1 = ahead ? last : need1;
    if (!ahead) while (s1 > s0 && (b->valid & (1u << (s1 - 1)))) s1--;
    if (fill_sectors(c, b, size, s0, s1, fill, ctx) < 0) {
      if (!b->valid) b->used = 0;                 /* nothing in it: first to be reused */
      return NULL;
    }
  }
  b->used = ++c->clock;
  return b;
}

long rd_archcache_read(rd_archcache *c, uint64_t key, int64_t size, int64_t off, void *buf,
                       size_t n, int ahead, rd_archcache_fill fill, void *ctx) {
  if (off < 0) return -1;
  if (off >= size || n == 0) return 0;
  if ((int64_t)n > size - off) n = (size_t)(size - off);
  size_t done = 0;
  while (done < n) {
    const int64_t pos = off + (int64_t)done;
    const int64_t index = pos / (int64_t)c->block_bytes;
    const size_t in = (size_t)(pos - index * (int64_t)c->block_bytes);
    size_t k = c->block_bytes - in;
    if (k > n - done) k = n - done;
    const rd_archcache_block *b = get(c, key, size, index, in, k, ahead, fill, ctx);
    if (!b) return done ? (long)done : -1;
    /* the valid run from `in` (shorter only when the file was shorter than `size`) */
    size_t run = 0;
    while (run < k && (b->valid & (1u << ((in + run) / c->sector_bytes)))) {
      const size_t sector_end = ((in + run) / c->sector_bytes + 1) * c->sector_bytes;
      run = sector_end - in;
    }
    if (run > k) run = k;
    if (run == 0) break;
    memcpy((uint8_t *)buf + done, b->data + in, run);
    done += run;
    if (run < k) break;
  }
  return (long)done;
}

int rd_archcache_is_archive(const char *path) {
  const char *ext = path ? strrchr(path, '.') : NULL;
  return ext && (strcasecmp(ext, ".obb") == 0 || strcasecmp(ext, ".apk") == 0);
}

void rd_archfiles_init(rd_archfiles *t, rd_archfd *fds, int nfds) {
  memset(fds, 0, (size_t)nfds * sizeof(*fds));
  *t = (rd_archfiles){ .fds = fds, .nfds = nfds };
}

int rd_archfiles_attach(rd_archfiles *t, int fd, uint64_t key, int64_t size) {
  if (fd < 0 || fd >= t->nfds) return 0;
  int i = 0;
  while (i < t->nfiles && t->files[i].key != key) i++;
  if (i == t->nfiles) {
    if (t->nfiles == RD_ARCHFILES_FILES) { t->fds[fd].file = 0; return 0; }
    t->files[i].key = key;
    t->nfiles++;
  }
  t->files[i].size = size;
  t->fds[fd] = (rd_archfd){ .file = (int8_t)(i + 1), .pos = 0, .last_end = -1 };
  return 1;
}

void rd_archfiles_detach(rd_archfiles *t, int fd) {
  if (fd >= 0 && fd < t->nfds) t->fds[fd].file = 0;
}

const rd_archfile *rd_archfiles_file(const rd_archfiles *t, int fd) {
  if (fd < 0 || fd >= t->nfds || !t->fds[fd].file) return NULL;
  return &t->files[t->fds[fd].file - 1];
}

int64_t rd_archfiles_pos(const rd_archfiles *t, int fd) {
  return rd_archfiles_file(t, fd) ? t->fds[fd].pos : 0;
}

void rd_archfiles_advance(rd_archfiles *t, int fd, int64_t n) {
  if (!rd_archfiles_file(t, fd) || n <= 0) return;
  t->fds[fd].pos += n;
  t->fds[fd].last_end = t->fds[fd].pos;
}

int rd_archfiles_sequential(const rd_archfiles *t, int fd) {
  return rd_archfiles_file(t, fd) && t->fds[fd].last_end == t->fds[fd].pos;
}

int64_t rd_archfiles_seek(rd_archfiles *t, int fd, int64_t off, int whence) {
  const rd_archfile *f = rd_archfiles_file(t, fd);
  if (!f) return -1;
  int64_t base;
  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = t->fds[fd].pos; break;
    case SEEK_END: base = f->size; break;
    default: return -1;
  }
  const int64_t np = base + off;
  if (np < 0) return -1;
  t->fds[fd].pos = np;
  return np;
}
