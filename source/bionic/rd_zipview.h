/* rd_zipview.h -- a ZIP archive with some entries replaced or added, without rewriting it.
 *
 * Mods replace game files inside base.apk / the OBB (rd_vfile.c serves the result to Unity).
 * Copying a 500 MB archive is too slow, so the view is the original archive up to its central
 * directory, then one stored entry per replaced or added file (local header + the mod file's
 * bytes), then a new central directory pointing replaced names at the appended entries, then
 * an end-of-central-directory record. The original entries stay where they were; only the
 * directory changes. The result is an ordinary ZIP (no ZIP64; such archives are refused).
 *
 * Pure (host-tested, tests/c/test_rd_zipview.c): the original archive and the mod files are
 * read through callbacks. MIT (see LICENSE). */
#ifndef RD_ZIPVIEW_H
#define RD_ZIPVIEW_H

#include <stddef.h>
#include <stdint.h>

/* Data offsets of appended entries are aligned like zipalign does, so a reader that maps a
 * stored entry gets an aligned pointer. */
#define RD_ZIPVIEW_ALIGN 16

/* One file to put into the archive: replaces the entry `name` or adds it. `src` is the caller's
 * index of the file's bytes, passed back to the read callback. */
typedef struct {
  const char *name;
  uint64_t    size;
  uint32_t    crc;     /* CRC-32 of the file's bytes */
  int         src;
} rd_zipview_file;

/* Where a range of the view comes from. */
enum { RD_ZV_ORIG, RD_ZV_MEM, RD_ZV_SRC };

typedef struct {
  int      kind;       /* RD_ZV_ORIG / RD_ZV_MEM / RD_ZV_SRC */
  int      src;        /* RD_ZV_SRC: the rd_zipview_file.src */
  uint64_t voff, len;  /* range in the view */
  uint64_t off;        /* ORIG: offset in the original; SRC: in the file; MEM: in mem */
} rd_zipview_seg;

typedef struct {
  rd_zipview_seg *segs;
  int             nsegs;
  uint8_t        *mem;       /* generated headers and directory */
  uint64_t        size;      /* total size of the view */
  int             replaced;  /* entries of the original that now point at a mod file */
  int             added;     /* new entries */
} rd_zipview;

/* Reads n bytes at `off` of the original archive (src < 0) or of file `src`. Returns the
 * number of bytes read (n unless at the end), or -1. */
typedef long (*rd_zipview_read_fn)(void *ctx, int src, uint64_t off, void *buf, size_t n);

/* Builds the view of an original archive of `orig_size` bytes with `n` files put in. A file
 * named twice is taken once (the first). Returns 0, -1 if the original is not a readable
 * ZIP, -2 for ZIP64 or a result that would need it, -3 out of memory. *v is valid only on 0;
 * free it with rd_zipview_free. */
int rd_zipview_build(rd_zipview *v, uint64_t orig_size, const rd_zipview_file *files, int n,
                     rd_zipview_read_fn read, void *ctx);

/* Reads up to n bytes of the view at `off` (fewer at the end). Returns the count, or -1 when a
 * source read fails. Thread-safe as long as `read` is. */
long rd_zipview_read(const rd_zipview *v, uint64_t off, void *buf, size_t n,
                     rd_zipview_read_fn read, void *ctx);

/* Releases the view's memory. */
void rd_zipview_free(rd_zipview *v);

#endif
