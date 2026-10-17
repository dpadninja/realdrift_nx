/* rd_vfile.c -- see rd_vfile.h. The view layout is rd_zipview.c (host-tested); this file is
 * the descriptor table, the FILE wrapper and the reads behind them. MIT (see LICENSE). */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <zlib.h>
#include "rd_platform.h"   /* RD_ARCHIVE_STDIO_BUFFER_BYTES */
#include "rd_vfile.h"
#include "rd_zipview.h"
#include "util.h"          /* debugPrintf */
#include "rd_log.h"

#define MAX_ARCHIVES 4
#define NAME_MAX_LEN 256
#define CRC_CHUNK    (64 * 1024)

typedef struct {
  int      ar;                      /* index in g_ar */
  char     member[NAME_MAX_LEN];
  char     path[NAME_MAX_LEN];
  uint64_t size;
  uint32_t crc;
  FILE    *fp;                      /* opened on first read */
} mod_file;

typedef struct {
  char        name[NAME_MAX_LEN];   /* archive file name, e.g. main.76....obb */
  int         state;                /* 0 not built, 1 view ready, -1 failed: use the original */
  FILE       *orig;
  rd_zipview  view;
} archive;

typedef struct { int used, ar; uint64_t pos; } vfd;

static mod_file g_files[RD_VFILE_MAX_FILES];
static int      g_nfiles;
static archive  g_ar[MAX_ARCHIVES];
static int      g_nar;
static vfd      g_fds[RD_VFILE_MAX_FDS];
static Mutex    g_lock;

/* The file name part of a path. */
static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

/* The archive called `name`, optionally created; NULL if absent (or the table is full). */
static archive *find_archive(const char *name, int create) {
  for (int i = 0; i < g_nar; i++)
    if (!strcmp(g_ar[i].name, name)) return &g_ar[i];
  if (!create || g_nar == MAX_ARCHIVES) return NULL;
  archive *a = &g_ar[g_nar++];
  memset(a, 0, sizeof *a);
  snprintf(a->name, sizeof a->name, "%s", name);
  return a;
}

/* Size and CRC-32 of a file; 0 or -1. */
static int file_crc(const char *path, uint64_t *size, uint32_t *crc) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  static unsigned char buf[CRC_CHUNK];
  uLong c = crc32(0, Z_NULL, 0);
  uint64_t n = 0;
  size_t r;
  while ((r = fread(buf, 1, sizeof buf, f)) > 0) { c = crc32(c, buf, (uInt)r); n += r; }
  const int err = ferror(f);
  fclose(f);
  if (err) return -1;
  *size = n;
  *crc = (uint32_t)c;
  return 0;
}

int rd_vfile_put(const char *archive_name, const char *member, const char *file) {
  if (g_nfiles == RD_VFILE_MAX_FILES) return -2;
  archive *a = find_archive(archive_name, 1);
  if (!a || a->state) return -2;
  mod_file *f = &g_files[g_nfiles];
  memset(f, 0, sizeof *f);
  if (strlen(member) >= sizeof f->member || strlen(file) >= sizeof f->path) return -1;
  f->ar = (int)(a - g_ar);
  strcpy(f->member, member);
  strcpy(f->path, file);
  if (file_crc(file, &f->size, &f->crc)) return -1;
  g_nfiles++;
  return 0;
}

int rd_vfile_count(const char *archive_name) {
  const archive *a = find_archive(archive_name, 0);
  int n = 0;
  for (int i = 0; a && i < g_nfiles; i++) n += g_files[i].ar == (int)(a - g_ar);
  return n;
}

/* rd_zipview read callback: the archive's original (src < 0) or a mod file; caller holds the
 * lock. */
static long read_cb(void *ctx, int src, uint64_t off, void *buf, size_t n) {
  FILE *f = ((archive *)ctx)->orig;
  if (src >= 0) {
    mod_file *m = &g_files[src];
    if (!m->fp) m->fp = fopen(m->path, "rb");
    f = m->fp;
  }
  if (!f || fseeko(f, (off_t)off, SEEK_SET)) return -1;
  const size_t r = fread(buf, 1, n, f);
  return ferror(f) ? -1 : (long)r;
}

/* Builds the view of archive `a` opened at `path` once; caller holds the lock. 1 if usable. */
static int ensure_view(archive *a, const char *path) {
  if (a->state) return a->state > 0;
  a->state = -1;
  a->orig = fopen(path, "rb");
  if (!a->orig) { debugPrintf(RD_LOG_TAG_MODS " %s: cannot open the original\n", a->name); return 0; }
  setvbuf(a->orig, NULL, _IOFBF, RD_ARCHIVE_STDIO_BUFFER_BYTES);
  fseeko(a->orig, 0, SEEK_END);
  const uint64_t size = (uint64_t)ftello(a->orig);
  rd_zipview_file *files = calloc((size_t)g_nfiles + 1, sizeof *files);
  int n = 0;
  for (int i = 0; files && i < g_nfiles; i++)
    if (g_files[i].ar == (int)(a - g_ar)) files[n++] = (rd_zipview_file){ g_files[i].member, g_files[i].size, g_files[i].crc, i };
  const int r = files ? rd_zipview_build(&a->view, size, files, n, read_cb, a) : -3;
  free(files);
  if (r) {
    debugPrintf(RD_LOG_TAG_MODS " %s: cannot build the mod view (%d), using the original\n", a->name, r);
    return 0;
  }
  a->state = 1;
  debugPrintf(RD_LOG_TAG_MODS " %s: %d files replaced, %d added (%llu -> %llu bytes)\n", a->name,
              a->view.replaced, a->view.added, (unsigned long long)size,
              (unsigned long long)a->view.size);
  return 1;
}

/* The archive behind `path` with a usable view, or NULL; caller holds the lock. */
static archive *view_for(const char *path) {
  archive *a = g_nfiles ? find_archive(base_name(path), 0) : NULL;
  return a && ensure_view(a, path) ? a : NULL;
}

int rd_vfile_open(const char *path) {
  if (!g_nfiles || !path) return -1;
  mutexLock(&g_lock);
  archive *a = view_for(path);
  int fd = -1;
  for (int i = 0; a && i < RD_VFILE_MAX_FDS; i++) {
    if (g_fds[i].used) continue;
    g_fds[i] = (vfd){ 1, (int)(a - g_ar), 0 };
    fd = RD_VFILE_FD_BASE + i;
    break;
  }
  mutexUnlock(&g_lock);
  if (a && fd < 0) debugPrintf(RD_LOG_TAG_MODS " %s: out of virtual descriptors\n", path);
  return fd;
}

int rd_vfile_is(int fd) { return fd >= RD_VFILE_FD_BASE && fd < RD_VFILE_FD_BASE + RD_VFILE_MAX_FDS; }

/* The open descriptor entry for fd, or NULL; caller holds the lock. */
static vfd *get(int fd) {
  if (!rd_vfile_is(fd)) return NULL;
  vfd *v = &g_fds[fd - RD_VFILE_FD_BASE];
  return v->used ? v : NULL;
}

long rd_vfile_pread(int fd, void *buf, size_t n, long off) {
  mutexLock(&g_lock);
  vfd *v = get(fd);
  long r = -1;
  if (v && off >= 0) {
    archive *a = &g_ar[v->ar];
    r = rd_zipview_read(&a->view, (uint64_t)off, buf, n, read_cb, a);
  }
  mutexUnlock(&g_lock);
  return r;
}

long rd_vfile_read(int fd, void *buf, size_t n) {
  mutexLock(&g_lock);
  vfd *v = get(fd);
  long r = -1;
  if (v) {
    archive *a = &g_ar[v->ar];
    r = rd_zipview_read(&a->view, v->pos, buf, n, read_cb, a);
    if (r > 0) v->pos += (uint64_t)r;
  }
  mutexUnlock(&g_lock);
  return r;
}

long rd_vfile_lseek(int fd, long off, int whence) {
  mutexLock(&g_lock);
  vfd *v = get(fd);
  long r = -1;
  if (v) {
    const int64_t base = whence == SEEK_SET ? 0
                       : whence == SEEK_CUR ? (int64_t)v->pos
                                            : (int64_t)g_ar[v->ar].view.size;
    if (base + off >= 0) { v->pos = (uint64_t)(base + off); r = (long)v->pos; }
  }
  mutexUnlock(&g_lock);
  return r;
}

int rd_vfile_fd_size(int fd, uint64_t *size) {
  mutexLock(&g_lock);
  vfd *v = get(fd);
  if (v) *size = g_ar[v->ar].view.size;
  mutexUnlock(&g_lock);
  return v ? 0 : -1;
}

int rd_vfile_path_size(const char *path, uint64_t *size) {
  if (!g_nfiles || !path) return -1;
  mutexLock(&g_lock);
  archive *a = view_for(path);
  if (a) *size = a->view.size;
  mutexUnlock(&g_lock);
  return a ? 0 : -1;
}

int rd_vfile_close(int fd) {
  mutexLock(&g_lock);
  vfd *v = get(fd);
  if (v) v->used = 0;
  mutexUnlock(&g_lock);
  return v ? 0 : -1;
}

/* ---- FILE wrapper (fopencookie) over a virtual descriptor ---- */

static ssize_t cookie_read(void *c, char *buf, size_t n) {
  return rd_vfile_read((int)(intptr_t)c, buf, n);
}
static int cookie_seek(void *c, off_t *off, int whence) {
  const long r = rd_vfile_lseek((int)(intptr_t)c, (long)*off, whence);
  if (r < 0) return -1;
  *off = r;
  return 0;
}
static int cookie_close(void *c) { return rd_vfile_close((int)(intptr_t)c); }

FILE *rd_vfile_fopen(const char *path) {
  const int fd = rd_vfile_open(path);
  if (fd < 0) return NULL;
  cookie_io_functions_t io = { cookie_read, NULL, cookie_seek, cookie_close };
  FILE *f = fopencookie((void *)(intptr_t)fd, "rb", io);
  if (!f) { rd_vfile_close(fd); return NULL; }
  setvbuf(f, NULL, _IOFBF, RD_ARCHIVE_STDIO_BUFFER_BYTES);
  return f;
}
