/* rd_archcache.h -- config.txt archive_cache: a block cache shared by every open of the game
 * archives (base.apk, the OBB).
 *
 * Unity opens the OBB hundreds of times per load and reads it in small pieces after seeks;
 * each FILE had its own 128 KB stdio buffer, refilled from the SD card after every seek, so a
 * track load read 115 MB to use 12 MB (analysis/research/26). Here a file is split into
 * fixed-size blocks, kept in a least-recently-used pool keyed by (file, block), and a read is
 * copied out of them; only what is missing is read through the caller's `fill`.
 *
 * A block is filled in sectors (a validity bit each). A read with `ahead` (stdio refills, which
 * go on nearby) fills its missing sectors to the end of the block in one request; one without
 * (read() / pread(), used at boot to walk zip headers spread over the whole OBB) fills only the
 * sectors it needs, since an SD request costs about the same up to tens of KB.
 *
 * rd_archfiles is the table beside it: which fds are archives, of which file, and each fd's
 * own position (read / lseek / pread on them never touch the real fd's position). A read() that
 * continues the previous one (inflating a compressed APK entry) also reads ahead.
 *
 * Pure logic, host-tested (tests/c/test_rd_archcache.c); not thread-safe (the caller locks).
 * MIT (see LICENSE). */
#ifndef RD_ARCHCACHE_H
#define RD_ARCHCACHE_H

#include <stddef.h>
#include <stdint.h>

#define RD_ARCHCACHE_MAX_SECTORS 32     /* sectors per block (bits of `valid`) */

/* Reads n bytes at `off` of the file into buf; returns the bytes read (fewer only at the end
 * of the file) or -1. */
typedef long (*rd_archcache_fill)(void *ctx, int64_t off, void *buf, size_t n);

/* One cached block: sectors of block `index` of file `key` (valid 0 = slot unused). */
typedef struct {
  uint64_t key;
  int64_t index;
  uint32_t valid;                 /* bit s: sector s holds file data */
  uint64_t used;                  /* `clock` at the last access (LRU) */
  uint8_t *data;                  /* block_bytes, allocated on first use and kept */
} rd_archcache_block;

typedef struct {
  rd_archcache_block *blocks;
  int nblocks;
  uint32_t block_bytes, sector_bytes;
  uint64_t clock;
  uint64_t hits, misses;          /* block lookups served from memory / that needed fill */
} rd_archcache;

/* Sets up c over the caller's array of nblocks entries (zeroed here) of block_bytes each,
 * filled in sector_bytes pieces (block_bytes a multiple of it, at most
 * RD_ARCHCACHE_MAX_SECTORS of them). */
void rd_archcache_init(rd_archcache *c, rd_archcache_block *blocks, int nblocks,
                       uint32_t block_bytes, uint32_t sector_bytes);

/* Copies n bytes at `off` of file `key` (`size` bytes long) into buf, reading missing sectors
 * through fill(ctx, ...), with `ahead` up to the end of their block. Returns the bytes copied
 * (fewer at the end of the file, 0 past it) or -1 when fill fails before anything was
 * copied. */
long rd_archcache_read(rd_archcache *c, uint64_t key, int64_t size, int64_t off, void *buf,
                       size_t n, int ahead, rd_archcache_fill fill, void *ctx);

/* 1 if `path` names a game archive: base.apk or an OBB (".apk" / ".obb", any case). */
int rd_archcache_is_archive(const char *path);

/* ---- fd table ----------------------------------------------------------------------- */

#define RD_ARCHFILES_FILES 8            /* distinct archive files */

typedef struct {
  uint64_t key;                         /* the path's hash: the cache key */
  int64_t size;
} rd_archfile;

typedef struct {
  int8_t file;                          /* index + 1 into rd_archfiles.files; 0 = not ours */
  int64_t pos;
  int64_t last_end;                     /* where the last read ended (-1: none yet) */
} rd_archfd;

typedef struct {
  rd_archfile files[RD_ARCHFILES_FILES];
  int nfiles;
  rd_archfd *fds;                       /* indexed by fd */
  int nfds;
} rd_archfiles;

/* Sets up t over the caller's array of nfds entries (zeroed here). */
void rd_archfiles_init(rd_archfiles *t, rd_archfd *fds, int nfds);

/* Makes fd an fd of file `key` (`size` bytes; a known key takes the new size) at position 0.
 * Returns 1, or 0 when fd is out of range or the file table is full (fd stays a plain fd). */
int rd_archfiles_attach(rd_archfiles *t, int fd, uint64_t key, int64_t size);

/* fd is a plain fd again (on close). */
void rd_archfiles_detach(rd_archfiles *t, int fd);

/* The file behind fd, or NULL when fd is not an archive fd. */
const rd_archfile *rd_archfiles_file(const rd_archfiles *t, int fd);

/* fd's position (0 when not an archive fd). */
int64_t rd_archfiles_pos(const rd_archfiles *t, int fd);

/* Moves fd's position past n bytes just read. */
void rd_archfiles_advance(rd_archfiles *t, int fd, int64_t n);

/* 1 if a read at fd's position continues the previous one (no seek in between): a sequential
 * reader, worth reading ahead for, as Linux's readahead decides. The first read is not. */
int rd_archfiles_sequential(const rd_archfiles *t, int fd);

/* lseek: whence SEEK_SET / SEEK_CUR / SEEK_END (from the file's size). Returns the new
 * position, or -1 (position unchanged) when it would be negative, whence is unknown or fd is
 * not an archive fd. */
int64_t rd_archfiles_seek(rd_archfiles *t, int fd, int64_t off, int whence);

#endif
