/* shim_fs.c -- the bionic file API on top of newlib: open/openat with flag translation,
 * read/write/close routing (fake fds, read-ahead), stat/fstat/lstat and dirent conversion to
 * the bionic aarch64 layouts, POSIX rename, realpath.
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#include "rd_platform.h"
#include "rd_device.h"
#include "util.h"
#include "imports.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "nx_fonts.h"     /* system fonts served as /system/fonts */
#include "fakefd.h"
#include "diag.h"
#include "rd_perfstat.h"


// ---------------------------------------------------------------------------
// open() flag translation (bionic/linux -> newlib)
// ---------------------------------------------------------------------------

#define LINUX_O_CREAT  0100
#define LINUX_O_EXCL   0200
#define LINUX_O_TRUNC  01000
#define LINUX_O_APPEND 02000

/* bionic/Linux O_* flags to newlib's values (only the access mode and the
 * creat/excl/trunc/append bits matter here). */
static int convert_open_flags(int flags) {
  int out = flags & 3;
  if (flags & LINUX_O_CREAT)  out |= O_CREAT;
  if (flags & LINUX_O_EXCL)   out |= O_EXCL;
  if (flags & LINUX_O_TRUNC)  out |= O_TRUNC;
  if (flags & LINUX_O_APPEND) out |= O_APPEND;
  return out;
}



int g_watch_fd = -1;   /* data.unity3d fd: trace its reads/seeks to debug header load */
void watch_dump(const char *tag, int fd, long a, long b, const void *buf, long got) {
  if (fd != g_watch_fd) return;
  char h[64]; int n = (got > 16 ? 16 : (got < 0 ? 0 : (int)got));
  int p = 0; for (int i = 0; i < n; i++) p += snprintf(h + p, sizeof(h) - p, "%02x ", ((const unsigned char *)buf)[i]);
  h[p] = 0;
  debugPrintf("[io] %s fd=%d a=%ld b=%ld -> %ld  [%s]\n", tag, fd, a, b, got, h);
}


int open_fake(const char *path, int flags, ...) {
  nx_stat_open++;
  path = nx_fonts_resolve(path);   /* Switch system fonts as /system/fonts */
  char _rbuf[512];
  path = casetest_redirect(path);
  path = asset_redirect(path, _rbuf, sizeof _rbuf);
  int mode = 0666;
  if (flags & LINUX_O_CREAT) { va_list va; va_start(va, flags); mode = va_arg(va, int); va_end(va); }
  const int cvt = convert_open_flags(flags);
  const int writing = (flags & 3) != 0 || (flags & LINUX_O_CREAT);
  if (writing && fs_write_refused(path)) return -1;
  if (!writing) {
    // /dev/urandom + /dev/random: Switch has no /dev node, but Mono/.NET (RNG
    // seeds, Guid.NewGuid, hashtable randomization) and asset crypto open these.
    // A failing open (-1) leaves those paths without entropy and can stall the
    // scene/asset load. Materialize a buffer of real CSPRNG bytes (libnx
    // randomGet) into a file and hand back a real fd so read() just works.
    if (!strcmp(path, "/dev/urandom") || !strcmp(path, "/dev/random")) {
      static char rbuf[65536];
      randomGet(rbuf, sizeof rbuf);
      char tf[256];
      snprintf(tf, sizeof tf, "%s/.synth_dev_random", RD_GAME_HOME);
      int wfd = open(tf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (wfd >= 0) { if (write(wfd, rbuf, sizeof rbuf) < 0) { /* best effort */ } close(wfd); }
      int rfd = open(tf, O_RDONLY);
      fd_ino_set(rfd, path);
      debugPrintf("[io] open(%s,0x%x) -> %d [urandom]\n", path, flags, rfd);
      return rfd;
    }
    // synthetic /proc, /sys (incl. self/maps)
    int sfd = synth_proc_open(path);
    if (sfd >= 0) { fd_ino_set(sfd, path); debugPrintf("[io] open(%s,0x%x) -> %d [synthetic]\n", path, flags, sfd); return sfd; }
  }
  /* Repeat read-only open of a file we are already holding: hand back a dup and skip the
   * directory lookup entirely (see OPEN-FD CACHE above). */
  if (!writing) {
    int cfd = ofdcache_open(path);
    if (cfd >= 0) {
      fd_ino_set(cfd, path);
      static unsigned ohits = 0;
      if (++ohits <= 3 || (ohits % 50) == 0)
        debugPrintf("[io] open(%s) -> %d [fd-cache, no SD lookup, hit #%u]\n", path, cfd, ohits);
      return cfd;
    }
  }

  int fd = open(path, cvt, mode);
  if (fd < 0 && writing) {
    // save files: the target subdir may not exist yet -- create it and retry
    mkdir_parents(path);
    fd = open(path, cvt, mode);
  }
  if (fd < 0 && (flags & 3) == 0 && !(flags & LINUX_O_CREAT)) {
    char alt[320];
    if (basename_fallback(path, alt, sizeof(alt)))
      fd = open(alt, cvt, mode);
  }
  if (fd >= 0) {
    if (!writing) ofdcache_adopt(path, fd);   /* may hold this handle open for reuse */
    fd_ino_set(fd, path);
    if (writing) mark_write_fd(fd, path);
    struct stat _st;
    if (fstat(fd, &_st) == 0) {
      debugPrintf("[io] open(%s,0x%x) -> %d size=%lld\n", path, flags, fd, (long long)_st.st_size);
      /* The game archives go through the shared block cache; other big read-only files
       * (data.unity3d ~424MB, sharedassets*.resource) get a read-ahead window, so Unity's
       * tiny per-field reads hit RAM, not SD. */
      if (!writing && arch_path(path))
        arch_attach(fd, path, (long)_st.st_size);
      else if (!writing && _st.st_size >= (4 << 20))
        ra_attach(fd, (long)_st.st_size);
    } else {
      debugPrintf("[io] open(%s,0x%x) -> %d size=?\n", path, flags, fd);
    }
  } else {
    debugPrintf("[io] open(%s,0x%x) -> %d\n", path, flags, fd);
  }
  return fd;
}
int openat_fake(int dirfd, const char *path, int flags, ...) {
  (void)dirfd;
  int mode = 0666;
  if (flags & LINUX_O_CREAT) { va_list va; va_start(va, flags); mode = va_arg(va, int); va_end(va); }
  // Delegate to open_fake so /dev/urandom, synthetic /proc + /sys, save-dir
  // creation and basename fallback all apply (some libc paths route open->openat).
  return open_fake(path, flags, mode);
}
int unlinkat_fake(int dirfd, const char *path, int flags) { (void)dirfd; (void)flags; return unlink(path); }

// ---------------------------------------------------------------------------
// struct stat conversion (bionic aarch64 layout)
// ---------------------------------------------------------------------------

struct bionic_timespec { int64_t tv_sec; int64_t tv_nsec; };
struct bionic_stat {
  uint64_t st_dev; uint64_t st_ino; uint32_t st_mode; uint32_t st_nlink;
  uint32_t st_uid; uint32_t st_gid; uint64_t st_rdev; uint64_t __pad1;
  int64_t st_size; int32_t st_blksize; int32_t __pad2; int64_t st_blocks;
  struct bionic_timespec st_atim; struct bionic_timespec st_mtim; struct bionic_timespec st_ctim;
  uint32_t __unused4; uint32_t __unused5;
};

/* newlib struct stat to the bionic aarch64 layout the engine reads (second-resolution times). */
static void convert_stat(const struct stat *in, struct bionic_stat *out) {
  memset(out, 0, sizeof(*out));
  out->st_dev = in->st_dev; out->st_ino = in->st_ino; out->st_mode = in->st_mode;
  out->st_nlink = in->st_nlink; out->st_uid = in->st_uid; out->st_gid = in->st_gid;
  out->st_rdev = in->st_rdev; out->st_size = in->st_size; out->st_blksize = in->st_blksize;
  out->st_blocks = in->st_blocks;
  out->st_atim.tv_sec = in->st_atime; out->st_mtim.tv_sec = in->st_mtime; out->st_ctim.tv_sec = in->st_ctime;
}

/* POSIX rename() REPLACES an existing destination. Horizon's fsFsRenameFile does NOT -- it
 * fails if the destination exists, and libnx's fsdev passes that straight through. Worse,
 * Horizon will not rename OR delete a file that currently has an open handle.
 *
 * A .NET transactional save (as Bad Piggies does it, where this was found):
 *     write Progress.dat.tmp
 *     if (File.Exists(Progress.dat)) File.Replace(tmp, Progress.dat, Progress.dat.bak);
 *     else                           File.Move   (tmp, Progress.dat);
 * and il2cpp's native ReplaceFile does:
 *     fd = open(backup, O_RDONLY);          <-- keeps .bak OPEN across the next call
 *     rename(dest -> backup);
 *     rename(source -> dest);
 *     close(fd);
 *
 * So on Switch that first rename hits a destination which (a) already exists and (b) is held
 * open by the caller itself. It cannot be renamed away and it cannot be deleted -- which is
 * why every save silently jammed, leaving the real save stranded in Progress.dat.tmp while
 * the game kept loading a stale Progress.dat. (The game try/catches its save, hence no
 * managed exception in the log.) Settings.xml goes down the identical path.
 *
 * The fix therefore must never touch the destination's DIRECTORY ENTRY -- only its CONTENTS.
 * We try, in order:
 *   1. plain rename                    (destination absent: nothing to do)
 *   2. park the destination aside, rename, drop the parked copy, roll back on failure
 *      (correct and near-atomic; works whenever the destination is not held open)
 *   3. copy the source's BYTES over the destination and unlink the source
 *      (the only thing that works when the destination has an open handle -- writing to an
 *       open file is allowed, renaming/deleting it is not)
 * Step 2 is kept ahead of step 3 because it is atomic and never leaves a half-written file;
 * step 3 is the fallback that actually rescues this game's save. We deliberately read the
 * source fully BEFORE truncating the destination, so a failure to read cannot destroy it. */
int rename_fake(const char *oldp, const char *newp) {
  char rb1[512], rb2[512];
  oldp = asset_redirect(oldp, rb1, sizeof rb1);
  newp = asset_redirect(newp, rb2, sizeof rb2);
  if (fs_write_refused(newp)) return -1;

  if (rename(oldp, newp) == 0) return 0;          /* 1. destination absent: plain rename */

  struct stat st;
  if (stat(newp, &st) != 0) {                     /* dest really is absent -> genuine error */
    debugPrintf("[io] rename(%s -> %s) FAILED, dest absent (errno %d)\n", oldp, newp, errno);
    return -1;
  }

  /* 2. park the destination aside */
  char aside[576];
  snprintf(aside, sizeof aside, "%s.rnold", newp);
  remove(aside);                                  /* stale one from an interrupted swap */
  if (rename(newp, aside) == 0) {
    if (rename(oldp, newp) == 0) {
      remove(aside);
      debugPrintf("[io] rename(%s -> %s) ok (displaced existing)\n", oldp, newp);
      return 0;
    }
    rename(aside, newp);                          /* roll back: restore the old file */
    debugPrintf("[io] rename(%s -> %s) FAILED, original restored (errno %d)\n", oldp, newp, errno);
    return -1;
  }

  /* 2b. The destination is held OPEN by the caller, and Horizon will not rename it, delete it,
   *     or even open it for writing (EIO) while a handle is live. il2cpp's native ReplaceFile is
   *     literally holding the file we have to replace:
   *         fd = open(backup, O_RDONLY);   rename(dest -> backup);   ... ;   close(fd);
   *     Nothing can be done to that file until the handle is gone -- so close it ourselves.
   *
   *     This is safe: the handle is a pure existence probe. ReplaceFile never reads a byte from
   *     it; its only other use is the close() afterwards, which on a stale fd returns EBADF and
   *     is ignored. To stop that fd NUMBER being handed to another thread in the gap, we re-open
   *     the destination right after the swap -- fds are allocated lowest-free, so we normally get
   *     the same number straight back and the caller's close() then closes our handle, which is
   *     exactly what it meant to do. */
  int victim = fd_close_by_path(newp);
  if (victim >= 0) {
    int ok = 0;
    if (rename(newp, aside) == 0) {
      if (rename(oldp, newp) == 0) { remove(aside); ok = 1; }
      else rename(aside, newp);                 /* roll back */
    }
    int re = open(newp, O_RDONLY);              /* re-occupy the fd number for the caller */
    if (re >= 0) {
      if (re == victim) fd_ino_set(re, newp);   /* caller's close() will close this */
      else close(re);
    }
    if (ok) {
      debugPrintf("[io] rename(%s -> %s) ok (closed caller's handle, then displaced)\n", oldp, newp);
      return 0;
    }
  }

  /* 3. still stuck -> overwrite the destination's contents instead of its directory entry.
   *    Read the source completely first; only then truncate the destination. */
  int fsrc = open(oldp, O_RDONLY);
  if (fsrc < 0) {
    debugPrintf("[io] rename(%s -> %s) FAILED, cannot read source (errno %d)\n", oldp, newp, errno);
    return -1;
  }
  struct stat ss;
  if (fstat(fsrc, &ss) != 0 || ss.st_size < 0) { close(fsrc); return -1; }
  size_t n = (size_t)ss.st_size;
  unsigned char *buf = (unsigned char *)malloc(n ? n : 1);
  if (!buf) { close(fsrc); return -1; }
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fsrc, buf + got, n - got);
    if (r <= 0) break;
    got += (size_t)r;
  }
  close(fsrc);
  if (got != n) {
    free(buf);
    debugPrintf("[io] rename(%s -> %s) FAILED, short read of source (%zu/%zu)\n", oldp, newp, got, n);
    return -1;                                    /* destination untouched */
  }

  int fdst = open(newp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fdst < 0) {
    free(buf);
    debugPrintf("[io] rename(%s -> %s) FAILED, cannot write dest (errno %d)\n", oldp, newp, errno);
    return -1;
  }
  size_t put = 0;
  while (put < n) {
    ssize_t w = write(fdst, buf + put, n - put);
    if (w <= 0) break;
    put += (size_t)w;
  }
  fsync(fdst);
  close(fdst);
  free(buf);
  if (put != n) {
    debugPrintf("[io] rename(%s -> %s) FAILED, short write of dest (%zu/%zu)\n", oldp, newp, put, n);
    return -1;
  }

  /* The destination now holds the source's bytes; rename() also means the source is gone. */
  if (remove(oldp) != 0)
    debugPrintf("[io] rename: warning, could not unlink source %s (errno %d)\n", oldp, errno);
  debugPrintf("[io] rename(%s -> %s) ok (%zu bytes copied over open dest)\n", oldp, newp, n);
  return 0;
}

/* opendir(): the font redirect must cover this too -- Android font discovery enumerates
 * /system/fonts, and nx_fonts.h needs EVERY filesystem entry point redirected, not just open(). */
void *opendir_fake(const char *path) {
  path = nx_fonts_resolve(path);   /* Switch system fonts as /system/fonts */
  return (void *)opendir(path);
}

int stat_fake(const char *path, struct bionic_stat *st) {
  path = nx_fonts_resolve(path);   /* Switch system fonts as /system/fonts */
  char _rbuf[512];
  path = asset_redirect(path, _rbuf, sizeof _rbuf);
  struct stat real; int r = stat(path, &real);
  if (r != 0) {
    char alt[320];
    if (basename_fallback(path, alt, sizeof(alt))) r = stat(alt, &real);
  }
  if (r == 0) {
    convert_stat(&real, st);
    if (st->st_ino == 0) st->st_ino = path_ino(path);   // fsdev gives 0 -> synth
  }
  return r;
}
int fstat_fake(int fd, struct bionic_stat *st) {
  struct stat real; const int r = fstat(fd, &real);
  if (r == 0) {
    convert_stat(&real, st);
    if (st->st_ino == 0) {                               // mirror stat(path)'s inode
      uint64_t ino = (fd >= 0 && fd < FD_INO_MAX) ? g_fd_ino[fd] : 0;
      st->st_ino = ino ? ino : ((uint64_t)(fd + 1) * 2654435761ULL) | 1;
    }
  }
  return r;
}
int lstat_fake(const char *path, struct bionic_stat *st) { return stat_fake(path, st); }

// ---------------------------------------------------------------------------
// dirent conversion (bionic dirent64 layout)
// ---------------------------------------------------------------------------

struct bionic_dirent {
  uint64_t d_ino; int64_t d_off; uint16_t d_reclen; uint8_t d_type; char d_name[256];
};

void *readdir_fake(void *dirp) {
  static struct bionic_dirent out; // not thread-safe (matches bionic readdir)
  struct dirent *e = readdir((DIR *)dirp);
  if (!e) return NULL;
  memset(&out, 0, sizeof(out));
  out.d_ino = e->d_ino;
  out.d_reclen = sizeof(out);
  out.d_type = e->d_type;
  snprintf(out.d_name, sizeof(out.d_name), "%s", e->d_name);
  return &out;
}


// ---------------------------------------------------------------------------
// filesystem odds and ends
// ---------------------------------------------------------------------------

char *realpath_fake(const char *path, char *resolved) {
  if (!path) return NULL;          /* POSIX: realpath(NULL,..) is an error, not a crash */
  if (!resolved) resolved = malloc(0x1000);
  strcpy(resolved, path);
  return resolved;
}
int strerror_r_fake(int err, char *buf, size_t len) { snprintf(buf, len, "%s", strerror(err)); return 0; }
int statvfs_fake(const char *path, void *buf) { (void)path; memset(buf, 0, 0x70); return 0; }
int statfs_fake(const char *path, void *buf) { (void)path; memset(buf, 0, 0x78); return 0; }

// ---------------------------------------------------------------------------
// fd routing: the native_app_glue command pipe lives in the fake-fd layer
// (android_native.c). Real files (small fds from open()) pass through to newlib.
// ---------------------------------------------------------------------------

static long read_fake_inner(int fd, void *buf, size_t count);
long read_fake(int fd, void *buf, size_t count) {          /* [perf] read= */
  uint64_t t0 = rd_now_ns();
  __atomic_add_fetch(&nx_stat_read, 1, __ATOMIC_RELAXED);            /* debug_load */
  long r = read_fake_inner(fd, buf, count);
  __atomic_add_fetch(&nx_stat_read_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  if (r > 0) __atomic_add_fetch(&nx_stat_read_bytes, (unsigned long long)r, __ATOMIC_RELAXED);
  return r;
}
/* read() routing: fake-pipe fds to fakefd.c, archives through the block cache, read-ahead fds
 * through their window, the rest to newlib. */
static long read_fake_inner(int fd, void *buf, size_t count) {
  if (fakefd_is_fake(fd)) return fakefd_read(fd, buf, count);
  if (arch_has(fd)) return arch_read(fd, buf, count, 0);
  { struct RaCache *c = ra_find(fd); if (c) return ra_read(c, fd, buf, count); }
  /* fsdev can return fewer bytes than requested for a large read; il2cpp's
   * global-metadata.dat loader (and others) assume a single read() fills the
   * buffer. Loop until `count` is satisfied or we hit EOF/error so the metadata
   * is never silently truncated (a short read leaves System.Object et al.
   * unresolvable -> Class::Init(NULL)). */
  size_t total = 0;
  while (total < count) {
    long r = read(fd, (char *)buf + total, count - total);
    if (r < 0) { if (total) break; return -1; }
    if (r == 0) break; /* EOF */
    total += (size_t)r;
  }
  if (count >= (1u << 20))
    debugPrintf("[io] read(fd=%d, %zu) -> %zu%s\n", fd, count, total,
                total < count ? "  *** SHORT READ ***" : "");
  watch_dump("read", fd, (long)count, 0, buf, (long)total);
  return (long)total;
}
long write_fake(int fd, const void *buf, size_t count) {
  if (fakefd_is_fake(fd)) return fakefd_write(fd, buf, count);
  const uint64_t t0 = rd_now_ns();                                   /* debug_load */
  const long r = write(fd, buf, count);
  __atomic_add_fetch(&nx_stat_write, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_write_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  if (r > 0) __atomic_add_fetch(&nx_stat_write_bytes, (unsigned long long)r, __ATOMIC_RELAXED);
  return r;
}
int close_fake(int fd) {
  /* Descriptor we are deliberately holding open for reuse (see OPEN-FD CACHE): swallow the
   * close. Keep g_fd_ino intact -- the mapping cache keys off it on the next open. */
  if (ofdcache_held(fd)) return 0;
  ra_detach(fd);
  arch_detach(fd);
  fd_ino_clear(fd);
  if (fakefd_is_fake(fd)) return fakefd_close(fd);
  int r = close(fd);
  commit_write_fd(fd);   /* mark the save dirty; nx_sd_flush() commits it (see above) */
  return r;
}
int pipe_fake(int fds[2]) { return fakefd_pipe(fds); }
int poll_fake(void *fds, unsigned long nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
int select_fake(int n, void *r, void *w, void *e, void *t) { (void)n; (void)r; (void)w; (void)e; (void)t; return 0; }
