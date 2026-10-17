/* shim_fs_cache.c -- per-descriptor state of the file-system shims: the read-ahead cache for
 * big archives, synthetic inode numbers, the open-fd cache for files Unity re-opens all the
 * time, and the SD-card commit after a written file is closed.
 *
 * Split out of libc_shim.c (inherited from the so-loader lineage, MIT). MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>

#include "util.h"
#include "imports.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "diag.h"
#include "rd_perfstat.h"


/* ---- read-ahead cache for big archive files (data.unity3d, sharedassets) ----
 * Unity deserializes archives with THOUSANDS of tiny read()s (2-8 bytes each,
 * field by field). On Android the archive sits in the OS page cache so these are
 * memory-fast; on Switch there is no page cache, so every tiny read is a direct
 * SD access (~ms) and boot crawls (8000+ reads just for the header). We buffer
 * each big read-only fd through a 1MB window filled by one large read, and
 * virtualize the logical file position -- turning ~250k tiny SD reads per MB into
 * a single one. Keyed by fd; the real fd position is used only as our scratch. */
#define RA_SLOTS 8
#define RA_WIN   (1u << 20)     /* 1 MB read-ahead window */
struct RaCache {
  int  fd;           /* -1 == free */
  long pos;          /* virtual file position (what read/lseek observe) */
  long size;         /* file size (for SEEK_END) */
  long base;         /* file offset of buf[0] */
  long len;          /* valid bytes currently in buf */
  unsigned char *buf;
};
static struct RaCache g_ra[RA_SLOTS];
static Mutex g_ra_lock;
struct RaCache *ra_find(int fd) {
  if (fd < 0) return NULL;
  for (int i = 0; i < RA_SLOTS; i++) if (g_ra[i].fd == fd) return &g_ra[i];
  return NULL;
}
void ra_attach(int fd, long size) {
  mutexLock(&g_ra_lock);
  for (int i = 0; i < RA_SLOTS; i++) if (g_ra[i].fd < 0) {
    if (!g_ra[i].buf) g_ra[i].buf = malloc(RA_WIN);
    if (g_ra[i].buf) { g_ra[i].fd = fd; g_ra[i].pos = 0; g_ra[i].size = size; g_ra[i].base = 0; g_ra[i].len = 0; }
    break;
  }
  mutexUnlock(&g_ra_lock);
}
void ra_detach(int fd) {
  mutexLock(&g_ra_lock);
  struct RaCache *c = ra_find(fd);
  if (c) c->fd = -1;   /* keep buf allocated for reuse */
  mutexUnlock(&g_ra_lock);
}
long ra_read(struct RaCache *c, int fd, void *buf, size_t count) {
  size_t done = 0;
  mutexLock(&g_ra_lock);
  while (done < count) {
    if (c->len == 0 || c->pos < c->base || c->pos >= c->base + c->len) {
      if (lseek(fd, c->pos, SEEK_SET) < 0) break;
      long r = 0;
      while (r < (long)RA_WIN) { long k = read(fd, c->buf + r, RA_WIN - r); if (k <= 0) break; r += k; }
      if (r <= 0) break;
      c->base = c->pos; c->len = r;
    }
    long avail = (c->base + c->len) - c->pos;
    if (avail <= 0) break;
    size_t n = (count - done < (size_t)avail) ? count - done : (size_t)avail;
    memcpy((char *)buf + done, c->buf + (c->pos - c->base), n);
    c->pos += n; done += n;
  }
  mutexUnlock(&g_ra_lock);
  return (long)done;
}

/* lseek for arm64: off_t is already 64-bit, so this also services lseek64.
 * lseek64 was previously stubbed to return 0 (no seek) -- that made libunity's
 * archive reader see data.unity3d as empty/mis-positioned ("Unable to read
 * header from archive file"), since it lseek64(SEEK_END)s to size the file. */
long z_lseek(int fd, long off, int whence) {
  struct RaCache *c = ra_find(fd);
  if (c) {   /* virtualized position -- don't touch the real fd here */
    mutexLock(&g_ra_lock);
    long np = (whence == SEEK_SET) ? off : (whence == SEEK_CUR) ? c->pos + off : c->size + off;
    c->pos = np;
    mutexUnlock(&g_ra_lock);
    return np;
  }
  return lseek(fd, off, whence);
}



// Synthetic inode numbers. libnx's fsdev returns st_ino==0 for EVERY file, but
// il2cpp's System.IO share layer keys its open-file table on (st_dev,st_ino):
// with every file colliding on inode 0 it treats UNRELATED files as the same
// one, so Easy Save's concurrent SaveData.es3.tmp (write) + SaveData.es3 (read)
// looks like an incompatible double-open of one file and throws "IOException:
// Sharing violation on path SaveData.es3" every frame on the menu -- and each of
// those exception backtraces mmaps libunity/libil2cpp, leaking us to OOM/exit.
// Give each distinct path a stable, non-zero inode (only when the real one is 0,
// so a genuine inode from a future fsdev is preserved). fstat() has no path, so a
// small fd->inode map is filled at open() time and mirrors stat(path)'s value.
uint64_t g_fd_ino[FD_INO_MAX];
uint64_t path_ino(const char *path) {
  uint64_t h = 1469598103934665603ULL;               // FNV-1a 64 offset basis
  for (const unsigned char *p = (const unsigned char *)path; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
  return h ? h : 1;                                   // 0 means "no inode" -- avoid it
}
void fd_ino_set(int fd, const char *path) { if (fd >= 0 && fd < FD_INO_MAX) g_fd_ino[fd] = path_ino(path); }
void fd_ino_clear(int fd) { if (fd >= 0 && fd < FD_INO_MAX) g_fd_ino[fd] = 0; }

/* Close every fd we know to be open on `path`, returning the LOWEST one we closed (or -1).
 * Used by rename_fake: Horizon refuses to rename, delete, or even open-for-write a file that
 * has a live handle, and il2cpp's ReplaceFile holds one on the very file we must replace.
 * g_fd_ino is a 64-bit FNV hash of the path, maintained by open_fake/close_fake, so a false
 * match would need a 64-bit hash collision against a path opened in this same process. */
int fd_close_by_path(const char *path) {
  const uint64_t ino = path_ino(path);
  int first = -1;
  for (int fd = 0; fd < FD_INO_MAX; fd++) {
    if (g_fd_ino[fd] != ino) continue;
    if (ofdcache_held(fd)) continue;      /* never close a deliberately-held handle */
    debugPrintf("[io] rename: closing caller's open handle fd=%d on %s\n", fd, path);
    close_fake(fd);
    if (first < 0) first = fd;
  }
  return first;
}


// libnx buffers sdmc: writes -- they reach the physical card only on fsdevCommitDevice() or a
// clean unmount. Exiting via HOME kills the process (title override), so that commit never runs
// and saves written this session vanish on reboot. Track fds opened for writing and commit the
// card when they close, so a save persists the moment the game finishes writing it.


#define WFD_MAX 4096
static unsigned char g_write_fd[WFD_MAX];
int nx_sd_dirty = 0;   /* a file has been opened for writing since the last commit */
void mark_write_fd(int fd)   { nx_sd_dirty = 1; if (fd >= 0 && fd < WFD_MAX) g_write_fd[fd] = 1; }

/* Close of a file that was written.
 *
 * This used to call fsdevCommitDevice("sdmc") INLINE, on every such close. That is a full SD
 * filesystem commit -- tens of milliseconds on Switch -- and it ran on the game thread, so any
 * action that produced a file write stalled the frame. The game writes on interaction (Flurry
 * analytics appends to FlurryLog.txt, PlayerPrefs -> prefs.kv, progress saves), which is why
 * discrete TAPS hitched while a sustained hold did not: a hold is one gesture, but each tap
 * completes an action that writes. A phone never shows this -- there is no fsdevCommitDevice
 * there; the OS just buffers in the page cache.
 *
 * The commit is also redundant. The port already commits from two places: the render loop every
 * 120 frames (~2s, dirty-gated) and nx_applet_hook on focus-change / exit-request, which covers
 * quitting via HOME. So we simply leave the dirty flag set and let those do the work -- saves
 * are still durable within ~2s and guaranteed on exit, without stalling the frame that wrote. */
void commit_write_fd(int fd) {
  if (fd < 0 || fd >= WFD_MAX || !g_write_fd[fd]) return;
  g_write_fd[fd] = 0;
  /* Commit the write to the physical card here, at close.
   *
   * I removed this earlier on the theory that it caused the tap stutter, and deferred it to the
   * periodic nx_sd_flush(). That theory was wrong -- the stutter was Unity re-reading its own
   * .so to symbolicate -- and deferring cost real durability: libnx buffers sdmc: writes, so
   * without a commit the save exists only in RAM. Combined with the rename jam, progress did not
   * survive a reload. Saves are rare (end of a level, settings change) and the tap path never
   * writes, so paying the commit here costs nothing during play and makes a save durable the
   * moment the game finishes writing it. nx_sd_flush() stays as the periodic safety net. */
  nx_sd_dirty = 0;
  nx_stat_commit++;
  fsdevCommitDevice("sdmc");
}

/* Commit any pending writes to the physical card. Called periodically from the render loop and
 * after the game's save (nx_save_prefs), so the save survives a HOME-kill / reboot. */
void nx_sd_flush(void) {
  if (!nx_sd_dirty) return;
  nx_sd_dirty = 0;
  nx_stat_commit++;
  fsdevCommitDevice("sdmc");
}

/* ---- OPEN-FD CACHE for repeatedly re-opened read-only files ---------------------------
 *
 * With the map dedup in place, Unity's symbolication no longer re-READS libunity.so /
 * libil2cpp.so -- but it still re-OPENS them (plus /proc/self/maps) on every tap, and an open
 * on Horizon is a FAT directory lookup plus a metadata read. Measured at ~40 ms for the three,
 * spiking past 400 ms when the card is slow. Since the mapping now comes from cache, the caller
 * never reads a byte from these fds: it opens, mmaps, closes.
 *
 * So keep one real handle per such file and hand out dup()s of it. dup() is a table entry, no
 * directory traversal. Same admission rule as the map cache -- only on the SECOND read-only
 * open of a path, so one-shot boot opens neither consume a slot nor stay open forever.
 *
 * The shared handle's file position is not a hazard here: mmap_fake saves and restores the
 * offset around its reads, and dup()ed descriptors are closed normally by the caller. If dup()
 * ever fails we simply fall through to a real open, so this can only ever be a speed-up. */
/* Two separate tables, for the same reason the map cache needed them: a small fixed array that
 * every one-shot BOOT open can fill is useless, because the file we actually care about is only
 * opened repeatedly LATER. Boot opens dozens of read-only files (assets, resources, metadata);
 * with a single 8-entry table they took every slot and libunity.so was never even recorded, so
 * the cache never engaged once (zero hits across two test builds).
 *   g_ofdc_seen : first-sighting ring, overwrite-oldest, cheap and disposable
 *   g_ofdc      : descriptors we are actually holding open -- never evicted */
#define OFDS_N 64
#define OFDC_N 8
static Mutex g_ofdc_lock;   /* own lock: g_fb_lock is declared further down the file */
static uint64_t g_ofdc_seen[OFDS_N];
static int g_ofdc_seen_n = 0, g_ofdc_seen_w = 0;
static struct { uint64_t ino; int fd; } g_ofdc[OFDC_N];
static int g_ofdc_n = 0;

/* 1 if we have seen this path opened read-only before; records it otherwise. */
static int ofdc_seen_check(uint64_t ino) {
  for (int i = 0; i < g_ofdc_seen_n; i++) if (g_ofdc_seen[i] == ino) return 1;
  if (g_ofdc_seen_n < OFDS_N) g_ofdc_seen[g_ofdc_seen_n++] = ino;
  else { g_ofdc_seen[g_ofdc_seen_w] = ino; g_ofdc_seen_w = (g_ofdc_seen_w + 1) % OFDS_N; }
  return 0;
}

/* Returns a cached handle for `path`, or -1. Records first sightings.
 *
 * NOTE: an earlier version handed out dup()s of the held handle. dup() does not work on
 * devkitPro/fsdev file descriptors -- it fails, so the adopt step below silently never fired
 * and the cache never engaged (zero hits in the log). We therefore hand back the SAME fd and
 * make close_fake() refuse to close a held one. Safe here because these files are opened
 * read-only and only to be mmap'd: the caller opens, maps (served from the map cache), and
 * closes. mmap_fake saves and restores the file offset around its reads, so a shared position
 * is not a hazard. */
/* ONLY shared libraries are ever held open.
 *
 * The cache exists for one thing: Unity re-opening libunity.so / libil2cpp.so to symbolicate a
 * native stack on every tap. An earlier version adopted any repeatedly-opened read-only file,
 * which quietly latched onto SAVE files -- Progress.dat.bak, Settings.xml.bak, fusion.registry.
 * Horizon cannot rename or delete a file that has a live handle, so holding .bak open jammed the
 * atomic save chain (write .tmp -> rename dat->bak -> rename tmp->dat) permanently. Sandbox
 * progress stopped persisting as a result. Restricting adoption to ".so" keeps every benefit and
 * cannot touch anything the game writes. */
static int ofdc_path_eligible(const char *path) {
  size_t n = path ? strlen(path) : 0;
  return n > 3 && !strcmp(path + n - 3, ".so");
}

int ofdcache_open(const char *path) {
  if (!ofdc_path_eligible(path)) return -1;
  const uint64_t ino = path_ino(path);
  int out = -1;
  mutexLock(&g_ofdc_lock);
  for (int i = 0; i < g_ofdc_n; i++)
    if (g_ofdc[i].ino == ino && g_ofdc[i].fd >= 0) { out = g_ofdc[i].fd; break; }
  mutexUnlock(&g_ofdc_lock);
  return out;
}

/* After a successful real read-only open: hold the handle if we have seen this path before and
 * there is a free slot. */
void ofdcache_adopt(const char *path, int fd) {
  if (!ofdc_path_eligible(path)) return;
  const uint64_t ino = path_ino(path);
  mutexLock(&g_ofdc_lock);
  for (int i = 0; i < g_ofdc_n; i++)
    if (g_ofdc[i].ino == ino) { mutexUnlock(&g_ofdc_lock); return; }   /* already held */
  if (ofdc_seen_check(ino) && g_ofdc_n < OFDC_N) {
    g_ofdc[g_ofdc_n].ino = ino; g_ofdc[g_ofdc_n].fd = fd; g_ofdc_n++;
    mutexUnlock(&g_ofdc_lock);
    debugPrintf("[io] fd-cache: holding %s open (repeat opens now free)\n", path);
    return;
  }
  mutexUnlock(&g_ofdc_lock);
}

/* Is this descriptor one we are deliberately holding open? close_fake() must not close it. */
int ofdcache_held(int fd) {
  int r = 0;
  mutexLock(&g_ofdc_lock);
  for (int i = 0; i < g_ofdc_n; i++) if (g_ofdc[i].fd == fd) { r = 1; break; }
  mutexUnlock(&g_ofdc_lock);
  return r;
}
