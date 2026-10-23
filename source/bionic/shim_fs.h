/* shim_fs.h -- shared helpers of the file-system shims (bionic layer, internal).
 *
 * The bionic file API (shim_fs.c, shim_stdio.c, shim_mmap.c) is built from two helper modules:
 *   shim_fs_path.c   rewrites and creates paths: asset root, case probe, /proc, mkdir -p;
 *   shim_fs_cache.c  keeps per-descriptor state: read-ahead windows, synthetic inodes, the
 *                    open-fd cache for .so files, and which fds were opened for writing.
 * Nothing outside source/bionic/ includes this header. All functions are thread-safe unless
 * noted. MIT (see LICENSE). */
#ifndef SHIM_FS_H
#define SHIM_FS_H

#include <stddef.h>
#include <stdint.h>

/* ---- paths (shim_fs_path.c) ---------------------------------------------------------- */

/* Unity's CASESENSITIVETEST<guid> probe files all map to one scratch file in the game folder;
 * any other path is returned unchanged. */
const char *casetest_redirect(const char *path);

/* "/assets/..." (the APK asset root on Android) -> the game folder's assets/; writes the new
 * path into buf (bufsz bytes) and returns buf, or returns `path` unchanged. */
const char *asset_redirect(const char *path, char *buf, size_t bufsz);

/* Inherited read fallback: if `path` has a directory part, the file of the same name in the game
 * folder. Returns 1 and the path in out (outsz bytes) if that file exists, else 0. */
int basename_fallback(const char *path, char *out, size_t outsz);

/* Creates the directory chain of a file path, never the SD root or the game root itself. */
void mkdir_parents(const char *filepath);

/* Text of a synthetic /proc or /sys file (meminfo, cpuinfo, CPU range, ...), or NULL if `path`
 * is not one we provide. Values come from rd_device.h. */
const char *synthetic_proc(const char *path);

/* open() for synthetic /proc and /sys nodes: materialises the content into a small file and
 * returns a real fd, or -1 if `path` is not synthesised (the caller opens normally). */
int synth_proc_open(const char *path);

/* ---- read-ahead cache (shim_fs_cache.c) ---------------------------------------------- */

/* A read-only big file served through a 1 MB window (Unity reads archives in tiny pieces). */
struct RaCache;

/* The read-ahead entry of fd, or NULL if fd is not cached. */
struct RaCache *ra_find(int fd);
/* Starts read-ahead for a freshly opened read-only fd of `size` bytes (if a slot is free). */
void ra_attach(int fd, long size);
/* Drops fd's read-ahead entry (on close). */
void ra_detach(int fd);
/* read() through the window: returns the bytes copied (0 at end of file or on a failed refill). */
long ra_read(struct RaCache *c, int fd, void *buf, size_t count);

/* ---- archive block cache (shim_fs_cache.c over rd_archcache.h) ----------------------- */

/* 1 if read-only opens of `path` go through the archive cache: archive_cache is on and the
 * path is base.apk or an OBB (by extension). */
int arch_path(const char *path);
/* Serves fd, just opened read-only on `path` (`size` bytes), from the cache at position 0;
 * the fd's own file position is then only scratch. No-op if the file table is full. */
void arch_attach(int fd, const char *path, long size);
/* Forgets fd (on close). */
void arch_detach(int fd);
/* 1 if fd is served from the cache. */
int arch_has(int fd);
/* read() at fd's position, which it advances: the bytes copied, 0 at the end, -1 on error.
 * With `ahead` (stdio refills), or when the read continues the previous one on fd, missing
 * data is read to the end of its cache block. */
long arch_read(int fd, void *buf, size_t n, int ahead);
/* pread(): like arch_read without `ahead` at `off`, the position untouched. */
long arch_pread(int fd, void *buf, size_t n, long off);
/* lseek() of fd's position: the new position, or -1 (errno EINVAL) if it would be negative. */
long arch_seek(int fd, long off, int whence);

/* ---- synthetic inodes ---------------------------------------------------------------- */

/* fsdev reports st_ino 0 for every file; il2cpp's file sharing table needs distinct inodes. */
#define FD_INO_MAX 4096
/* Inode of the path each fd was opened with (0 = unknown); see fd_ino_set. */
extern uint64_t g_fd_ino[FD_INO_MAX];
/* Stable non-zero inode for a path (64-bit FNV-1a of the path). */
uint64_t path_ino(const char *path);
/* Records the inode of the path fd was opened with, for fstat. */
void fd_ino_set(int fd, const char *path);
/* Forgets fd's inode (on close). */
void fd_ino_clear(int fd);
/* Closes every fd known to be open on `path` except the ones the open-fd cache holds
 * (Horizon cannot rename a file with a live handle). Returns the lowest fd closed, or -1. */
int fd_close_by_path(const char *path);

/* ---- open-fd cache (.so files Unity re-opens to symbolicate stacks) ------------------- */

/* An fd already held open for `path`, or -1. */
int ofdcache_open(const char *path);
/* After a real read-only open: hold fd if this path was opened before and a slot is free. */
void ofdcache_adopt(const char *path, int fd);
/* 1 if fd is held by the cache; close() must leave such fds open. */
int ofdcache_held(int fd);

/* ---- SD-card commit after writing ---------------------------------------------------- */

/* Remembers that fd was opened for writing on `path` (and marks the card dirty). */
void mark_write_fd(int fd, const char *path);
/* On close: commits the SD card if fd was written, so a save is durable at once (files
 * rd_sdcommit_on_close excludes wait for nx_sd_flush). */
void commit_write_fd(int fd);

#endif
