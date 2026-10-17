/* shim_stdio.c -- stdio for the engine: fopen with path redirects and synthetic /proc
 * files, and fread/fwrite/fprintf over the fake bionic __sF streams.
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>

#include "config.h"        /* DEBUG_LOG */
#include "rd_platform.h"
#include "util.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "nx_fonts.h"     /* system fonts served as /system/fonts */
#include "rd_synth.h"
#include "rd_perfstat.h"



// fopen with the port's path redirects (fonts, assets, synthetic /proc) and a large stdio
// buffer for the game archives.
FILE *fopen_fake(const char *path, const char *mode) {
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
  if (writing) mark_write_fd(fileno(f));
  debugPrintf("[io] fopen(%s,%s) -> %p\n", path, mode, (void *)f);
  if (strchr(mode, 'r')) {
    const char *ext = strrchr(path, '.');
    /* Unity reads the APK/OBB zip through stdio with many small reads (headers, object
     * tables); newlib's default 1 KB buffer made each one an SD request. */
    if (ext && (strcasecmp(ext, ".obb") == 0 || strcasecmp(ext, ".apk") == 0))
      setvbuf(f, NULL, _IOFBF, RD_ARCHIVE_STDIO_BUFFER_BYTES);
  }
  return f;
}

// ---------------------------------------------------------------------------
// stdio over the fake bionic __sF (stdin/stdout/stderr). libc++_shared wires
// std::cout/cerr/cin to &__sF[1]/[2]/[0]; these wrappers absorb writes to those
// fake FILEs and forward everything else to newlib.
// ---------------------------------------------------------------------------

uint8_t fake_sF[3][0x100]; // referenced by imports.c (__sF / std{in,out,err})

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
  return fwrite(ptr, size, n, f);
}
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) return 0;
  uint64_t t0 = rd_now_ns();                               /* [perf] read= */
  size_t r = fread(ptr, size, n, f);
  __atomic_add_fetch(&nx_stat_read_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_read_bytes, (unsigned long long)r * size, __ATOMIC_RELAXED);
  return r;
}
int fputc_fake(int c, FILE *f) { if (is_fake_file(f)) return c; return fputc(c, f); }
int fputs_fake(const char *s, FILE *f) { if (is_fake_file(f)) { debugPrintf("stdio: %s", s); return 0; } return fputs(s, f); }
int fflush_fake(FILE *f) { if (is_fake_file(f) || f == NULL) return 0; return fflush(f); }
int fclose_fake(FILE *f) { if (is_fake_file(f)) return 0;
  int fd = fileno(f); fd_ino_clear(fd);   /* keep g_fd_ino free of stale entries: fd_close_by_path
                                             (rename_fake) closes fds by that table */
  int r = fclose(f); commit_write_fd(fd); return r; }
int ferror_fake(FILE *f) { if (is_fake_file(f)) return 0; return ferror(f); }
int feof_fake(FILE *f) { if (is_fake_file(f)) return 1; return feof(f); }
int fileno_fake(FILE *f) { if (is_fake_file(f)) return ((const uint8_t *)f - &fake_sF[0][0]) / 0x100; return fileno(f); }
int fseek_fake(FILE *f, long off, int whence) { if (is_fake_file(f)) return -1; return fseek(f, off, whence); }
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
