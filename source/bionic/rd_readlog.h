/* rd_readlog.h -- archive_cache: a launch prefetch of the game archives' small reads.
 *
 * At every launch Unity walks base.apk and the OBB with thousands of small read()s at the same
 * places (the OBB's 1771 local zip headers, ...): ~3800 SD requests for 1.6 MB, ~2 s, nearly
 * all of it the card's per-request latency (analysis/research/26). The first launch records
 * those reads (file key, offset, bytes); the log is written once into one file, and later
 * launches read it in one request and serve the same reads from memory -- what Windows'
 * Prefetcher and Android's iorap do for app launches. A stamp (sizes and modification times
 * of both archives) throws the log away when an archive changes.
 *
 * Pure logic, host-tested (tests/c/test_rd_readlog.c); not thread-safe (the caller locks).
 * MIT (see LICENSE). */
#ifndef RD_READLOG_H
#define RD_READLOG_H

#include <stddef.h>
#include <stdint.h>

#define RD_READLOG_STAMP_MAX 160        /* bytes of a stamp, NUL included */

/* One recorded read: `len` bytes at `off` of file `key`, its bytes at data + data_at. */
typedef struct {
  uint64_t key;
  int64_t off;
  uint32_t len;
  uint32_t data_at;
} rd_readlog_range;

/* The recorder: caller-owned arrays of `cap` ranges and `data_cap` bytes. */
typedef struct {
  rd_readlog_range *ranges;
  int n, cap;
  uint8_t *data;
  size_t used, data_cap;
} rd_readlog;

/* A loaded log: ranges sorted by (key, off), disjoint, pointing into the file's bytes. */
typedef struct {
  const uint8_t *ranges;          /* n packed entries in the file */
  int n;
  const uint8_t *data;
} rd_readlog_view;

/* The stamp text for the two archives into buf (n bytes); its length, or -1 if too small. */
int rd_readlog_stamp(char *buf, size_t n, int64_t apk_size, int64_t apk_mtime, int64_t obb_size,
                     int64_t obb_mtime);

/* Sets up a recorder over the caller's arrays. */
void rd_readlog_init(rd_readlog *r, rd_readlog_range *ranges, int cap, uint8_t *data,
                     size_t data_cap);

/* Records that n bytes `buf` were read at `off` of file `key`. Returns 0 when the recorder is
 * full (the read is not recorded), else 1. */
int rd_readlog_add(rd_readlog *r, uint64_t key, int64_t off, const void *buf, size_t n);

/* Writes the log file for the recorded reads under `stamp` into out (cap bytes): ranges sorted,
 * overlapping or touching ones merged. Returns its size, or 0 if it does not fit. With
 * out == NULL returns the size it needs. Sorts r's ranges. */
size_t rd_readlog_serialize(rd_readlog *r, const char *stamp, uint8_t *out, size_t cap);

/* Checks a log file of len bytes: format, sizes and `stamp`. Returns 1 and fills v (which points
 * into buf) when it is valid for these archives, else 0. */
int rd_readlog_parse(rd_readlog_view *v, const uint8_t *buf, size_t len, const char *stamp);

/* The n bytes at `off` of file `key` when the log holds all of them, else NULL. */
const uint8_t *rd_readlog_find(const rd_readlog_view *v, uint64_t key, int64_t off, size_t n);

#endif
