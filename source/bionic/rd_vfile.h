/* rd_vfile.h -- the game archives (base.apk, the OBB) as Unity sees them with mod files put in.
 *
 * Mods replace or add archive members (mods/<id>/files/apk|obb/...). Before Unity starts, the
 * mod layer registers each such file here (rd_vfile_put). Opening an archive that has files
 * registered then returns a virtual descriptor or FILE over an rd_zipview of it; every other
 * open is untouched. The fs shims route the virtual descriptors here: read, pread, lseek,
 * fstat/stat size, close and the file-backed mmap fill.
 *
 * Virtual descriptors are numbers >= RD_VFILE_FD_BASE (fakefd.c uses another range). All calls
 * are thread-safe (one lock; reads of the original and of mod files go through it).
 * MIT (see LICENSE). */
#ifndef RD_VFILE_H
#define RD_VFILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RD_VFILE_FD_BASE   0x50000000
#define RD_VFILE_MAX_FDS   32
#define RD_VFILE_MAX_FILES 512   /* registered mod files over all archives */

/* Registers `file` (a path on the SD card) as member `member` of the archive whose file name
 * is `archive` (e.g. RD_GAME_OBB_NAME). Call before Unity opens the archive. Reads the file
 * once for its size and CRC. Returns 0, -1 if the file cannot be read, -2 if the table is
 * full or the archive is already open. */
int rd_vfile_put(const char *archive, const char *member, const char *file);

/* Number of files registered for archive `archive` (file name). */
int rd_vfile_count(const char *archive);

/* A virtual descriptor for `path` if its file name is an archive with files registered, else
 * -1 (the caller opens the real file). Builds the view on first use; if that fails (not a ZIP,
 * ZIP64) the error is logged once and the original is used. */
int rd_vfile_open(const char *path);

/* Same, as a FILE (fopencookie); NULL when `path` is not virtual or on error. */
FILE *rd_vfile_fopen(const char *path);

/* 1 if fd is a virtual descriptor. */
int rd_vfile_is(int fd);

/* read / pread / lseek on a virtual descriptor, like the POSIX calls (-1 for a bad fd). */
long rd_vfile_read(int fd, void *buf, size_t n);
long rd_vfile_pread(int fd, void *buf, size_t n, long off);
long rd_vfile_lseek(int fd, long off, int whence);

/* Size of the view behind fd, or of the view of `path` (0 and *size, -1 if not virtual). */
int rd_vfile_fd_size(int fd, uint64_t *size);
int rd_vfile_path_size(const char *path, uint64_t *size);

/* Closes a virtual descriptor; 0 or -1. */
int rd_vfile_close(int fd);

#endif
