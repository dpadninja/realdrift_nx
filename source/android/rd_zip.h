/* rd_zip.h -- read single entries of a ZIP (the game's base.apk). Host-testable
 * (tests/c/test_rd_zip.c).
 *
 * The fake AssetManager (unity_jni.c) serves assets straight from base.apk instead of an
 * extracted copy on the SD card. Entries may be stored or deflated (Real Drift's APKs deflate
 * almost everything under assets/).
 * MIT (see LICENSE). */
#ifndef RD_ZIP_H
#define RD_ZIP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint64_t data_offset;   /* file offset of the entry's (possibly compressed) data */
  uint64_t comp_size;
  uint64_t size;          /* uncompressed size */
  int      method;        /* 0 = stored, 8 = deflate */
} rd_zip_entry;

/* Finds `name` (exact path, e.g. "assets/bin/Data/boot.config") in the central directory.
 * Returns 0 and fills *e, -1 if absent or the file is not a readable ZIP. */
int rd_zip_find(FILE *f, const char *name, rd_zip_entry *e);

/* Reads the whole entry, inflating if needed. Returns a malloc'd buffer (caller frees) of
 * e->size bytes (+1 NUL, not counted), or NULL on error / unsupported method. */
void *rd_zip_read(FILE *f, const rd_zip_entry *e);

#endif
