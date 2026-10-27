/* shim_stdio.c -- stdio for the engine: fopen with path redirects and synthetic /proc
 * files, and fread/fwrite/fprintf over the fake bionic __sF streams.
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>
#include <sys/stat.h>

#include "config.h"        /* DEBUG_LOG */
#include "rd_platform.h"
#include "util.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "rd_archcache.h"   /* rd_archcache_is_archive */
#include "nx_fonts.h"     /* system fonts served as /system/fonts */
#include "rd_synth.h"
#include "rd_perfstat.h"



/* archive_cache: newlib reads and seeks a FILE through its _read / _seek hooks, with the FILE
 * itself as the cookie. Pointed at the block cache, the stdio buffer refills from RAM while
 * fileno() and fstat() keep the real fd. No host test: these are fields of newlib's __sFILE64
 * (glibc's FILE has none); the cache and fd table behind them are tested (rd_archcache). */
static _READ_WRITE_RETURN_TYPE arch_sread(struct _reent *r, void *cookie, char *buf,
                                          _READ_WRITE_BUFSIZE_TYPE n) {
  (void)r;
  return arch_read(((FILE *)cookie)->_file, buf, n, 1);
}

static _fpos_t arch_sseek(struct _reent *r, void *cookie, _fpos_t off, int whence) {
  (void)r;
  return arch_seek(((FILE *)cookie)->_file, off, whence);
}

#ifdef __LARGE64_FILES
static _fpos64_t arch_sseek64(struct _reent *r, void *cookie, _fpos64_t off, int whence) {
  (void)r;
  return arch_seek(((FILE *)cookie)->_file, off, whence);
}
#endif

/* Buffering of a freshly opened read-only FILE of `path`: the game archives read through the
 * block cache with a small buffer, or (archive_cache=0, or the cache's file table full) with a
 * large one, since newlib's default 1 KB made each of Unity's small reads an SD request. */
static void archive_buffering(FILE *f, const char *path) {
  if (!rd_archcache_is_archive(path)) return;
  struct stat st;
  if (arch_path(path) && fstat(fileno(f), &st) == 0) {
    arch_attach(fileno(f), path, (long)st.st_size);
    if (arch_has(fileno(f))) {
      f->_read = arch_sread;            /* before setvbuf, which looks at _seek */
      f->_seek = arch_sseek;
#ifdef __LARGE64_FILES
      f->_seek64 = arch_sseek64;
#endif
      setvbuf(f, NULL, _IOFBF, RD_ARCHIVE_CACHED_STDIO_BYTES);
      return;
    }
  }
  setvbuf(f, NULL, _IOFBF, RD_ARCHIVE_STDIO_BUFFER_BYTES);
}

// fopen with the port's path redirects (fonts, assets, synthetic /proc) and the game archives
// through the block cache (archive_buffering).
FILE *fopen_fake(const char *path, const char *mode) {
  __atomic_add_fetch(&nx_stat_fopen, 1, __ATOMIC_RELAXED);           /* debug_load */
  path = nx_fonts_resolve(path);   /* Switch system fonts as /system/fonts */
  char _rbuf[512];
  path = asset_redirect(path, _rbuf, sizeof _rbuf);
  /* Was fmemopen(); its fileno() is -1, which broke readers that do
   * read(fileno(f)) -- Unity 2018.4's gnustl std::ifstream("/proc/cpuinfo") threw
   * ios_base::failure. Serve the same materialized file synth_proc_open() uses. */
  if (synthetic_proc(path)) {
    FILE *sf = rd_fopen_fd(synth_proc_open(path));
    debugPrintf("[io] fopen(%s) -> %p [synthetic]\n", path, (void *)sf);
    return sf;
  }
  const int writing = strpbrk(mode, "wa+") != NULL;
  if (writing && fs_write_refused(path)) return NULL;
  FILE *f = fopen(path, mode);
  if (!f && writing) {            // save file: create the subdir and retry
    mkdir_parents(path);
    f = fopen(path, mode);
  }
  if (!f && !writing && strchr(mode, 'r')) {
    char alt[320];
    if (basename_fallback(path, alt, sizeof(alt)))
      f = fopen(alt, mode);
  }
  if (!f)
    return NULL;
  if (writing) mark_write_fd(fileno(f), path);
  debugPrintf("[io] fopen(%s,%s) -> %p\n", path, mode, (void *)f);
  if (!writing) archive_buffering(f, path);
  return f;
}

// ---------------------------------------------------------------------------
// stdio over the fake bionic __sF (stdin/stdout/stderr). libc++_shared wires
// std::cout/cerr/cin to &__sF[1]/[2]/[0]; these wrappers absorb writes to those
// fake FILEs and forward everything else to newlib.
// ---------------------------------------------------------------------------

uint8_t fake_sF[3][0x100]; // referenced by imports.c (__sF / std{in,out,err})

/* 1 if f is one of the engine's stdin/stdout/stderr (&fake_sF[0..2]), which go to debug.log
 * instead of newlib. */
static int is_fake_file(const void *f) {
  const uint8_t *p = f;
  const uint8_t *base = (const uint8_t *)fake_sF;
  return p >= base && p < base + sizeof(fake_sF);
}

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) {
#if DEBUG_LOG
    static char buf[0x400];
    const size_t total = size * n < sizeof(buf) - 1 ? size * n : sizeof(buf) - 1;
    memcpy(buf, ptr, total); buf[total] = '\0';
    debugPrintf("stdio: %s", buf);
#endif
    return n;
  }
  const uint64_t t0 = rd_now_ns();                                   /* debug_load */
  const size_t r = fwrite(ptr, size, n, f);
  __atomic_add_fetch(&nx_stat_write, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_write_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_write_bytes, (unsigned long long)(r * size), __ATOMIC_RELAXED);
  return r;
}
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) return 0;
  uint64_t t0 = rd_now_ns();                               /* [perf] read= */
  __atomic_add_fetch(&nx_stat_fread, 1, __ATOMIC_RELAXED);
  size_t r = fread(ptr, size, n, f);
  __atomic_add_fetch(&nx_stat_read_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_read_bytes, (unsigned long long)r * size, __ATOMIC_RELAXED);
  return r;
}
int fputc_fake(int c, FILE *f) { if (is_fake_file(f)) return c; return fputc(c, f); }
int fputs_fake(const char *s, FILE *f) { if (is_fake_file(f)) { debugPrintf("stdio: %s", s); return 0; } return fputs(s, f); }
int fflush_fake(FILE *f) { if (is_fake_file(f) || f == NULL) return 0; return fflush(f); }
int fclose_fake(FILE *f) { if (is_fake_file(f)) return 0;
  int fd = fileno(f); fd_ino_clear(fd); arch_detach(fd);   /* keep g_fd_ino free of stale entries: fd_close_by_path
                                             (rename_fake) closes fds by that table */
  int r = fclose(f); commit_write_fd(fd); return r; }
int ferror_fake(FILE *f) { if (is_fake_file(f)) return 0; return ferror(f); }
int feof_fake(FILE *f) { if (is_fake_file(f)) return 1; return feof(f); }
int fileno_fake(FILE *f) { if (is_fake_file(f)) return ((const uint8_t *)f - &fake_sF[0][0]) / 0x100; return fileno(f); }
int fseek_fake(FILE *f, long off, int whence) {
  if (is_fake_file(f)) return -1;
  __atomic_add_fetch(&nx_stat_fseek, 1, __ATOMIC_RELAXED);            /* debug_load */
  return fseek(f, off, whence);
}
long ftell_fake(FILE *f) { if (is_fake_file(f)) return -1; return ftell(f); }
int getc_fake(FILE *f) { if (is_fake_file(f)) return -1; return getc(f); }
int fgetc_fake(FILE *f) { if (is_fake_file(f)) return -1; return fgetc(f); }
char *fgets_fake(char *s, int n, FILE *f) { if (is_fake_file(f)) return NULL; return fgets(s, n, f); }
int ungetc_fake(int c, FILE *f) { if (is_fake_file(f)) return -1; return ungetc(c, f); }

int fprintf_fake(FILE *f, const char *fmt, ...) {
  va_list va; va_start(va, fmt);
  int ret;
  if (is_fake_file(f)) {
#if DEBUG_LOG
    static char buf[0x400];
    ret = vsnprintf(buf, sizeof(buf), fmt, va);
    debugPrintf("stdio: %s", buf);
#else
    ret = 0;
#endif
  } else {
    ret = vfprintf(f, fmt, va);
  }
  va_end(va);
  return ret;
}
int vfprintf_fake(FILE *f, const char *fmt, va_list va) {
  if (is_fake_file(f)) {
#if DEBUG_LOG
    static char buf[0x400];
    int ret = vsnprintf(buf, sizeof(buf), fmt, va);
    debugPrintf("stdio: %s", buf);
    return ret;
#else
    return 0;
#endif
  }
  return vfprintf(f, fmt, va);
}
