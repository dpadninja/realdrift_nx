/* shim_fs_cache.c -- per-descriptor state of the file-system shims: the read-ahead cache for
 * big archives, synthetic inode numbers, the open-fd cache for files Unity re-opens all the
 * time, and the SD-card commit after a written file is closed.
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <errno.h>
#include <string.h>

#include "util.h"
#include "imports.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "diag.h"
#include "rd_perfstat.h"
#include "rd_platform.h"
#include "rd_archcache.h"
#include "rd_sdcommit.h"
#include "rd_readlog.h"
#include "rd_game.h"
#include <stdio.h>
#include <sys/stat.h>
#include "config.h"        /* archive_cache */


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

/* ---- archive block cache (rd_archcache.h) ----
 * Every open of base.apk / the OBB shares one block pool. Each fd has a virtual position; a
 * missing block is read through the fd that asked for it (its real position is scratch), all
 * under one lock. */
#define ARCH_BLOCKS (RD_ARCHIVE_CACHE_BYTES / RD_ARCHIVE_CACHE_BLOCK)
static rd_archfd g_arch_fd[FD_INO_MAX];
static rd_archfiles g_arch_files;
static rd_archcache_block g_arch_blocks[ARCH_BLOCKS];
static rd_archcache g_arch;
static Mutex g_arch_lock;

int arch_path(const char *path) {
  return config.archive_cache && rd_archcache_is_archive(path);
}

void arch_attach(int fd, const char *path, long size) {
  mutexLock(&g_arch_lock);
  if (!g_arch.blocks) {
    rd_archcache_init(&g_arch, g_arch_blocks, ARCH_BLOCKS, RD_ARCHIVE_CACHE_BLOCK,
                      RD_ARCHIVE_CACHE_SECTOR);
    rd_archfiles_init(&g_arch_files, g_arch_fd, FD_INO_MAX);
  }
  rd_archfiles_attach(&g_arch_files, fd, path_ino(path), size);
  mutexUnlock(&g_arch_lock);
}

void arch_detach(int fd) {
  mutexLock(&g_arch_lock);
  if (g_arch.blocks) rd_archfiles_detach(&g_arch_files, fd);
  mutexUnlock(&g_arch_lock);
}

int arch_has(int fd) {
  mutexLock(&g_arch_lock);
  const int r = g_arch.blocks && rd_archfiles_file(&g_arch_files, fd) != NULL;
  mutexUnlock(&g_arch_lock);
  return r;
}

/* rd_archcache fill: n bytes at `off` through the fd in ctx (newlib, looping on short reads). */
static long arch_fill(void *ctx, int64_t off, void *buf, size_t n) {
  const int fd = (int)(intptr_t)ctx;
  if (lseek(fd, (off_t)off, SEEK_SET) < 0) return -1;
  size_t got = 0;
  while (got < n) {
    const ssize_t r = read(fd, (char *)buf + got, n - got);
    if (r < 0) return got ? (long)got : -1;
    if (r == 0) break;
    got += (size_t)r;
  }
  return (long)got;
}

/* The cache read for fd at `off`, publishing the hit / miss counts for debug_load; caller
 * holds g_arch_lock. */
static long arch_read_locked(int fd, void *buf, size_t n, long off, int ahead) {
  const rd_archfile *f = g_arch.blocks ? rd_archfiles_file(&g_arch_files, fd) : NULL;
  if (!f) return -1;
  const long r = rd_archcache_read(&g_arch, f->key, f->size, off, buf, n, ahead, arch_fill,
                                   (void *)(intptr_t)fd);
  __atomic_store_n(&nx_stat_cache_hit, g_arch.hits, __ATOMIC_RELAXED);
  __atomic_store_n(&nx_stat_cache_miss, g_arch.misses, __ATOMIC_RELAXED);
  return r;
}

/* ---- launch prefetch (rd_readlog.h) ----
 * The first archive read loads <game>/RD_READLOG_FILE if its stamp matches both archives
 * (replay); otherwise the small reads of the next RD_READLOG_RECORD_NS are recorded and the file
 * is written once (a valid log is never rewritten). All under g_arch_lock. */
enum { RL_UNSET, RL_REPLAY, RL_RECORD, RL_OFF };
extern int nx_sd_dirty;                  /* the SD commit section below */
static int g_rl = RL_UNSET;
static rd_readlog_view g_rl_view;
static uint8_t *g_rl_file;               /* the loaded log (replay), kept for the run */
static rd_readlog g_rl_rec;
static char g_rl_stamp[RD_READLOG_STAMP_MAX];
static uint64_t g_rl_until;

/* The whole file at `path` (malloc'd) and its size in *len, or NULL. */
static uint8_t *read_whole(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t *buf = NULL;
  long size = -1;
  if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
  if (size > 0 && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)size))) {
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); buf = NULL; }
  }
  fclose(f);
  *len = buf ? (size_t)size : 0;
  return buf;
}

static void readlog_start_locked(void) {
  g_rl = RL_OFF;
  struct stat a, o;
  if (stat(RD_GAME_HOME "/" RD_BASE_APK_NAME, &a) || stat(RD_GAME_HOME "/" RD_GAME_OBB_NAME, &o) ||
      rd_readlog_stamp(g_rl_stamp, sizeof g_rl_stamp, a.st_size, a.st_mtime, o.st_size,
                       o.st_mtime) < 0)
    return;
  const uint64_t t0 = rd_now_ns();
  size_t len = 0;
  g_rl_file = read_whole(RD_GAME_HOME "/" RD_READLOG_FILE, &len);
  if (g_rl_file && rd_readlog_parse(&g_rl_view, g_rl_file, len, g_rl_stamp)) {
    g_rl = RL_REPLAY;
    debugPrintf("[io] readlog: %d ranges, %zu KB prefetched in %llu ms\n", g_rl_view.n, len >> 10,
                (unsigned long long)((rd_now_ns() - t0) / RD_NS_PER_MS));
    return;
  }
  free(g_rl_file);
  g_rl_file = NULL;
  rd_readlog_range *ranges = malloc(RD_READLOG_MAX_RANGES * sizeof *ranges);
  uint8_t *data = malloc(RD_READLOG_MAX_BYTES);
  if (!ranges || !data) { free(ranges); free(data); return; }
  rd_readlog_init(&g_rl_rec, ranges, RD_READLOG_MAX_RANGES, data, RD_READLOG_MAX_BYTES);
  g_rl = RL_RECORD;
  g_rl_until = rd_now_ns() + RD_READLOG_RECORD_NS;
  debugPrintf("[io] readlog: %s, recording this launch's small archive reads\n",
              len ? "stale" : "none yet");
}

/* Writes the recorded log once and stops recording. */
static void readlog_finish_locked(void) {
  g_rl = RL_OFF;
  const size_t need = rd_readlog_serialize(&g_rl_rec, g_rl_stamp, NULL, 0);
  uint8_t *out = malloc(need);
  const size_t len = out ? rd_readlog_serialize(&g_rl_rec, g_rl_stamp, out, need) : 0;
  FILE *f = len ? fopen(RD_GAME_HOME "/" RD_READLOG_FILE, "wb") : NULL;
  int ok = 0;
  if (f) { ok = fwrite(out, 1, len, f) == len; ok &= fclose(f) == 0; }
  if (f) nx_sd_dirty = 1;               /* committed by the next nx_sd_flush */
  debugPrintf("[io] readlog %s: %d reads, %zu KB\n", ok ? "written" : "NOT written", g_rl_rec.n,
              len >> 10);
  if (f && !ok) remove(RD_GAME_HOME "/" RD_READLOG_FILE);
  free(out);
  free(g_rl_rec.ranges);
  free(g_rl_rec.data);
  g_rl_rec = (rd_readlog){ 0 };
}

/* A read of n bytes at `off` of fd from the prefetched log: n, or 0 when it is not there. */
static long readlog_serve_locked(int fd, void *buf, size_t n, long off) {
  if (g_rl == RL_UNSET) readlog_start_locked();
  const rd_archfile *f = g_rl == RL_REPLAY ? rd_archfiles_file(&g_arch_files, fd) : NULL;
  const uint8_t *p = f ? rd_readlog_find(&g_rl_view, f->key, off, n) : NULL;
  if (!p || !n) return 0;
  memcpy(buf, p, n);
  __atomic_add_fetch(&nx_stat_readlog_hit, 1, __ATOMIC_RELAXED);
  return (long)n;
}

/* After a cache read of r bytes at `off`: records it while recording (small reads only). */
static void readlog_note_locked(int fd, const void *buf, long r, long off, int ahead) {
  if (g_rl != RL_RECORD) return;
  const rd_archfile *f = rd_archfiles_file(&g_arch_files, fd);
  if (f && r > 0 && !ahead && r <= RD_READLOG_MAX_READ &&
      !rd_readlog_add(&g_rl_rec, f->key, off, buf, (size_t)r)) {
    readlog_finish_locked();                       /* full: what we have is the log */
    return;
  }
  if (rd_now_ns() > g_rl_until) readlog_finish_locked();
}

long arch_read(int fd, void *buf, size_t n, int ahead) {
  mutexLock(&g_arch_lock);
  const long pos = (long)rd_archfiles_pos(&g_arch_files, fd);
  long r = readlog_serve_locked(fd, buf, n, pos);
  if (!r) {
    ahead = ahead || rd_archfiles_sequential(&g_arch_files, fd);
    r = arch_read_locked(fd, buf, n, pos, ahead);
    readlog_note_locked(fd, buf, r, pos, ahead);
  }
  if (r > 0) rd_archfiles_advance(&g_arch_files, fd, r);
  mutexUnlock(&g_arch_lock);
  return r;
}

long arch_pread(int fd, void *buf, size_t n, long off) {
  mutexLock(&g_arch_lock);
  long r = readlog_serve_locked(fd, buf, n, off);
  if (!r) {
    r = arch_read_locked(fd, buf, n, off, 0);
    readlog_note_locked(fd, buf, r, off, 0);
  }
  mutexUnlock(&g_arch_lock);
  return r;
}

long arch_seek(int fd, long off, int whence) {
  mutexLock(&g_arch_lock);
  const int64_t np = rd_archfiles_seek(&g_arch_files, fd, off, whence);
  mutexUnlock(&g_arch_lock);
  if (np < 0) { errno = EINVAL; return -1; }
  return (long)np;
}

/* lseek for arm64: off_t is already 64-bit, so this also services lseek64.
 * lseek64 was previously stubbed to return 0 (no seek) -- that made libunity's
 * archive reader see data.unity3d as empty/mis-positioned ("Unable to read
 * header from archive file"), since it lseek64(SEEK_END)s to size the file. */
long z_lseek(int fd, long off, int whence) {
  if (arch_has(fd)) return arch_seek(fd, off, whence);
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
#define WFD_COMMIT 1                  /* commit on close */
#define WFD_LATER  2                  /* left to nx_sd_flush (rd_sdcommit.h) */
void mark_write_fd(int fd, const char *path) {
  nx_sd_dirty = 1;
  if (fd >= 0 && fd < WFD_MAX) g_write_fd[fd] = rd_sdcommit_on_close(path) ? WFD_COMMIT : WFD_LATER;
}

/* fsdevCommitDevice, counted and timed for [perf] / [load]. */
static void commit_sd(void) {
  const uint64_t t0 = rd_now_ns();
  nx_stat_commit++;
  fsdevCommitDevice("sdmc");
  __atomic_add_fetch(&nx_stat_commit_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
}

/* Close of a file that was written: commit the SD card now.
 *
 * libnx buffers sdmc: writes; without a commit a save exists only in RAM and is lost if the
 * process is killed (HOME). A commit costs tens of milliseconds on the game thread, but saves are
 * rare (end of a level, a settings change) and the tap path never writes, so paying it here is
 * free during play and makes a save durable the moment the game finishes writing it.
 * nx_sd_flush() (every RD_SD_FLUSH_FRAMES and on exit) stays as the safety net.
 * History: the commit was once deferred to nx_sd_flush() on the theory that it caused tap
 * stutter; the stutter was Unity re-reading its own .so to symbolicate, and deferring lost
 * progress across reloads. */
void commit_write_fd(int fd) {
  if (fd < 0 || fd >= WFD_MAX || !g_write_fd[fd]) return;
  const int now = g_write_fd[fd] == WFD_COMMIT;
  g_write_fd[fd] = 0;
  if (!now) return;                   /* nx_sd_dirty stays set: the next flush commits it */
  nx_sd_dirty = 0;
  commit_sd();
}

/* Commit any pending writes to the physical card. Called periodically from the render loop and
 * after the game's save (nx_save_prefs), so the save survives a HOME-kill / reboot. */
void nx_sd_flush(void) {
  if (!nx_sd_dirty) return;
  nx_sd_dirty = 0;
  commit_sd();
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
