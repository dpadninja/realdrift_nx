/* shim_mmap.c -- mmap/munmap/mprotect/madvise for the engine: picks the overcommit window
 * (shim_mmap_oc.c) or the heap arena (shim_mmap_arena.c), falls back to memalign for small
 * maps when the arena is full, and deduplicates read-only file maps (il2cpp maps its own .so
 * files again for every managed stack trace).
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>
#include <errno.h>
#include <malloc.h>

#include "config.h"
#include "util.h"
#include "imports.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "rd_perfstat.h"
#include "shim_mmap.h"

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------

int posix_memalign_fake(void **out, size_t align, size_t size) {
  void *p = memalign(align, size);
  if (!p) return ENOMEM;
  *out = p;
  return 0;
}

#define MMAP_FALLBACK_MAX 4096
static struct { void *ptr; size_t len; } g_fb[MMAP_FALLBACK_MAX];
static int   g_fb_n = 0;
static size_t g_fb_bytes = 0;
static Mutex g_fb_lock;

/* When the arena is full: a memalign'd block (256 MB-aligned for big anonymous maps, which
 * Unity's pools need), filled like an arena map and remembered so munmap can free it. NULL if
 * the heap is exhausted. */
static void *mmap_fallback(size_t length, int flags, int fd, long offset) {
  /* Big anonymous reservations are Unity Dynamic-Heap pools: its allocator masks
   * pointers down to a large-aligned pool base to derive block indices, so a
   * 4KB-aligned block makes that math walk off into unmapped memory (the
   * libunity+0xdce75c Data Abort). Hand big anon maps a MMAP_BIG_ALIGN-aligned
   * base so the index math lands correctly even out here in the newlib overflow. */
  size_t align = (length >= MMAP_BIG_THRESH && (flags & BIONIC_MAP_ANONYMOUS))
                   ? MMAP_BIG_ALIGN : MMAP_PAGE;
  void *q = memalign(align, length);
  if (!q) return NULL;
  long got = 0;
  if (flags & BIONIC_MAP_ANONYMOUS) {
    memset(q, 0, length);
  } else {
    if (fd >= 0) {
      long cur = lseek(fd, 0, SEEK_CUR);
      if (lseek(fd, offset, SEEK_SET) >= 0)
        while ((size_t)got < length) { long r = read(fd, (char *)q + got, length - got); if (r <= 0) break; got += r; }
      if (cur >= 0) lseek(fd, cur, SEEK_SET);
    }
    if ((size_t)got < length) memset((char *)q + got, 0, length - got);
  }
  mutexLock(&g_fb_lock);
  if (g_fb_n < MMAP_FALLBACK_MAX) { g_fb[g_fb_n].ptr = q; g_fb[g_fb_n].len = length; g_fb_n++; g_fb_bytes += length; }
  const size_t total = g_fb_bytes;
  mutexUnlock(&g_fb_lock);
  debugPrintf("[mmap] fallback %u KB -> %p  anon=%d fd=%d off=0x%lx got=%ld (total %u MB)\n",
              (unsigned)(length >> 10), q, !!(flags & BIONIC_MAP_ANONYMOUS), fd, offset, got,
              (unsigned)(total >> 20));
  return q;
}

// returns 1 and frees if addr was a fallback allocation
static int mmap_fallback_free(void *addr) {
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_fb_n; i++) {
    if (g_fb[i].ptr == addr) {
      free(addr);
      g_fb_bytes -= g_fb[i].len;
      g_fb[i] = g_fb[--g_fb_n];
      mutexUnlock(&g_fb_lock);
      return 1;
    }
  }
  mutexUnlock(&g_fb_lock);
  return 0;
}

// ---- read-only file-map dedup cache ---------------------------------------
// il2cpp builds a stack trace for every thrown managed exception (even caught
// ones -- GPG login retries, etc.), and the symbolizer mmaps libil2cpp.so
// (~40MB) + libunity.so (~25MB) read-only EACH TIME, never munmapping. Over a
// play session that is dozens of fresh copies (~1.4GB) that exhaust newlib ->
// the "arena FULL / out of RAM" self-exit. Dedup: hand back one shared, pinned
// buffer per (file inode, offset, length). Safe -- these maps are read-only.
#define MAPC_N 48
/* Admission: a file is cached only on its SECOND map.
 *
 * The cache is finite and mapcache_put() silently no-ops when full. Admitting every read-only
 * file map let ~20 one-shot BOOT maps (global-metadata.dat, data.unity3d, sharedassets*, ...)
 * race for the slots, so whether libunity.so still found one by the time you tapped depended on
 * what that boot happened to load first -- which is why the stall came back on roughly half the
 * boots. One-shot maps also must not be pinned: pinning makes them permanent and raises peak
 * memory for no benefit, since nothing ever maps them again.
 *
 * So we keep a tiny signature ring of files we have mapped once. Only when the SAME
 * (file, offset, length) is mapped a second time does it earn a cache slot -- which is exactly
 * the repeat-mapping behaviour (libunity.so symbolication) worth paying for. */
#define MAPS_N 64
static struct { uint64_t ino; long off; size_t len; } g_maps_seen[MAPS_N];
static int g_maps_seen_n = 0;

/* Returns 1 if this signature has been mapped before (=> admit to the cache). Records it
 * otherwise. Ring-overwrites oldest; a lost record only costs one extra map. */
static int mapseen_check_and_record(uint64_t ino, long off, size_t len) {
  int seen = 0;
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_maps_seen_n; i++)
    if (g_maps_seen[i].ino == ino && g_maps_seen[i].off == off && g_maps_seen[i].len == len) {
      seen = 1; break;
    }
  if (!seen) {
    int slot = (g_maps_seen_n < MAPS_N) ? g_maps_seen_n++ : (rand() % MAPS_N);
    g_maps_seen[slot].ino = ino; g_maps_seen[slot].off = off; g_maps_seen[slot].len = len;
  }
  mutexUnlock(&g_fb_lock);
  return seen;
}

static struct { uint64_t ino; long off; size_t len; void *ptr; } g_mapc[MAPC_N];
static int g_mapc_n = 0;
/* The cached read-only map of (inode, offset, length), or NULL. */
static void *mapcache_get(uint64_t ino, long off, size_t len) {
  void *r = NULL;
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_mapc_n; i++)
    if (g_mapc[i].ino == ino && g_mapc[i].off == off && g_mapc[i].len == len) { r = g_mapc[i].ptr; break; }
  mutexUnlock(&g_fb_lock);
  return r;
}
/* 1 if ptr is a cached map; munmap must keep it. */
static int mapcache_contains(void *ptr) {
  int r = 0;
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_mapc_n; i++) if (g_mapc[i].ptr == ptr) { r = 1; break; }
  mutexUnlock(&g_fb_lock);
  return r;
}
/* Pins a fully read read-only map in the cache (dropping it from the fallback list so it is
 * never freed); warns once when the cache is full. */
static void mapcache_put(uint64_t ino, long off, size_t len, void *ptr) {
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_fb_n; i++)          // pin: drop from fallback free-list
    if (g_fb[i].ptr == ptr) { g_fb_bytes -= g_fb[i].len; g_fb[i] = g_fb[--g_fb_n]; break; }
  if (g_mapc_n >= MAPC_N) {
    static int warned = 0;
    if (!warned) { warned = 1;
      debugPrintf("[mmap] WARNING: map cache full (%d entries) -- repeat maps will re-read from SD\n",
                  MAPC_N); }
  }
  if (g_mapc_n < MAPC_N) { g_mapc[g_mapc_n].ino = ino; g_mapc[g_mapc_n].off = off;
                           g_mapc[g_mapc_n].len = len; g_mapc[g_mapc_n].ptr = ptr; g_mapc_n++; }
  mutexUnlock(&g_fb_lock);
}

void *mmap_fake(void *addr, size_t length, int prot, int flags, int fd, long offset) {
  nx_stat_mmap++;
  (void)addr;
  if (length == 0) length = 1;

  /* READ-ONLY FILE MAP DEDUP -- checked FIRST, before we allocate or touch the SD card.
   *
   * Unity symbolicates native stack frames by re-opening its own libunity.so and mapping the
   * whole thing, then reading /proc/self/maps for the load address. Something the game does on
   * a TAP triggers that (a caught exception or a logged event -- it never prints, so it is
   * invisible without instrumentation). Each occurrence re-read the entire 17 MB file off the
   * SD card on the game thread: measured at ~244 ms, i.e. a 15-frame stall, on every tap.
   *
   * The port already had this dedup, but only on the arena-EXHAUSTED fallback path -- these
   * maps succeed in the arena, so they never reached it. Hoisting the check here makes the
   * second and subsequent maps of the same file free, and also stops us leaking a fresh 17 MB
   * copy each time (29 maps in one short session was ~500 MB of address space).
   *
   * Safe because the map is read-only: every caller gets the same immutable bytes. Cached
   * pointers are pinned -- munmap_fake leaves them alone (see below). */
  uint64_t mc_ino = 0;   /* non-zero => this is a read-only file map we can dedup */
  int      mc_admit = 0; /* 1 => we have mapped this exact file before: cache it this time */
  {
    int ro_file = fd >= 0 && !(flags & BIONIC_MAP_ANONYMOUS) && !(prot & BIONIC_PROT_WRITE);
    mc_ino = (ro_file && fd < FD_INO_MAX) ? g_fd_ino[fd] : 0;
    if (mc_ino) {
      void *hit = mapcache_get(mc_ino, offset, length);
      if (hit) {
        static unsigned hits = 0;
        if (++hits <= 3 || (hits % 50) == 0)
          debugPrintf("[mmap] file map fd=%d len=%zu -> CACHED (no SD read, hit #%u)\n",
                      fd, length, hits);
        return hit;
      }
      mc_admit = mapseen_check_and_record(mc_ino, offset, length);
    }
  }

  // Big anonymous PROT_NONE reservations -> stack-region OC arena: cheap address
  // space now, physical aliased in on the later mprotect(RW). On a full window we
  // fall through to the heap-backed arena below (no behaviour change there).
  if (oc_pages && length >= MMAP_BIG_THRESH &&
      (flags & BIONIC_MAP_ANONYMOUS) && prot == BIONIC_PROT_NONE) {
    size_t ocres = 0;
    mutexLock(&g_mmap_lock);
    void *op = oc_alloc_locked(length, &ocres);
    mutexUnlock(&g_mmap_lock);
    if (op) {
      debugPrintf("[mmap] %u MB (prot=0x0 anon=1) -> %p  [OC reserve %u MB]\n",
                  (unsigned)(length >> 20), op, (unsigned)(ocres >> 20));
      return op;   // reserved-only; committed lazily via mprotect_fake
    }
    debugPrintf("[mmap] OC window full for %u MB -> heap-backed arena\n",
                (unsigned)(length >> 20));
  }

  size_t reserved = 0;
  mutexLock(&g_mmap_lock);
  mmap_arena_init_locked();
  void *p = mmap_arena_alloc_locked(length, &reserved);
  mutexUnlock(&g_mmap_lock);
  if (length >= MMAP_BIG_THRESH)
    debugPrintf("[mmap] %u MB (prot=0x%x anon=%d) -> %p  [reserved %u MB]\n",
                (unsigned)(length >> 20), prot, !!(flags & BIONIC_MAP_ANONYMOUS), p,
                (unsigned)(reserved >> 20));
  /* A file-backed map must be contiguous and fully readable. When the arena can
   * only give a tail-overflow reservation (reserved < length) we'd read just
   * `fill` bytes and leave the tail unfilled -- silently truncating the file in
   * RAM. For global-metadata.dat that nulls out System.Object (Class::Init NULL).
   * Hand any short-reserved file map to newlib, which backs the whole length. */
  if (p && !(flags & BIONIC_MAP_ANONYMOUS) && fd >= 0 && reserved < length) {
    mutexLock(&g_mmap_lock);
    mmap_arena_free(p, length);
    mutexUnlock(&g_mmap_lock);
    debugPrintf("[mmap] file fd=%d len=%zu: arena tail-overflow (reserved=%zu) -> newlib\n",
                fd, length, reserved);
    p = NULL;
  }
  if (!p) {
    // Arena exhausted (Unity's pools fill it). Route the request to newlib's free
    // heap regardless of size -- il2cpp's resource-extraction maps can exceed 64MB,
    // and rejecting them is what NULL-derefs the engine. Only a genuinely huge map
    // (> newlib free) will fail, and we log that distinctly.
    // Read-only file maps get deduped (backtrace .so symbolication leak, see above).
    void *q = mmap_fallback(length, flags, fd, offset);
    if (q) { if (mc_ino && mc_admit) mapcache_put(mc_ino, offset, length, q); return q; }
    debugPrintf("[mmap] arena FULL and newlib fallback FAILED for %u MB (out of RAM)\n",
                (unsigned)(length >> 20));
    errno = ENOMEM; return (void *)-1;
  }

  // Never touch beyond what we actually reserved in-arena (tail over-maps reserve
  // less than the requested length; the spill lives past the arena and is trimmed).
  size_t fill = length < reserved ? length : reserved;

  if (flags & BIONIC_MAP_ANONYMOUS) {
    memset(p, 0, fill);   // anonymous memory must read back as zero
  } else {
    // File-backed mapping: pull [offset, offset+fill) into RAM (no real mmap).
    long got = 0;
    if (fd >= 0) {
      long cur = lseek(fd, 0, SEEK_CUR);
      if (lseek(fd, offset, SEEK_SET) >= 0) {
        while ((size_t)got < fill) {
          long r = read(fd, (char *)p + got, fill - (size_t)got);
          if (r <= 0) break;
          got += r;
        }
      }
      if (cur >= 0) lseek(fd, cur, SEEK_SET);
    }
    if ((size_t)got < fill) memset((char *)p + got, 0, fill - (size_t)got);
    if (fd >= 0)
      debugPrintf("[mmap] file map fd=%d len=%zu reserved=%zu fill=%zu got=%ld%s\n",
                  fd, length, reserved, fill, got,
                  ((size_t)got < length) ? "  *** TRUNCATED ***" : "");
    /* Cache it so a repeat map of the same file is free. Only fully-read read-only maps:
     * a truncated one must not be handed out again as if it were complete. */
    if (mc_ino && mc_admit && (size_t)got >= length)
      mapcache_put(mc_ino, offset, length, p);
  }
  return p;
}

int munmap_fake(void *addr, size_t length) {
  /* Pinned read-only file map: keep it so the next map of the same file is free. Freeing it
   * would leave a dangling pointer in the cache and bring the 17 MB SD re-read back. */
  if (mapcache_contains(addr)) return 0;
  if (mmap_fallback_free(addr)) return 0;   // newlib fallback allocation
  if (oc_contains(addr)) {                   // stack-region OC reservation
    mutexLock(&g_mmap_lock);
    oc_free_locked(addr, length);
    mutexUnlock(&g_mmap_lock);
    return 0;
  }
  mmap_arena_free(addr, length);            // unreserve address space
  return 0;
}

// mprotect: the heap arena is always RW, so only the overcommit window acts on it (commits on
// RW; PROT_NONE is left mapped, Unity never decommits). The rest is a diagnostic.
int mprotect_fake(void *addr, size_t len, int prot) {
  /* Diagnostic (fires even heap-backed): measure Unity's commit pattern so we can
   * confirm it commits PROT_NONE reservations via mprotect(RW) and size the
   * overcommit commit-pool. Tracks cumulative RW-commit vs PROT_NONE-decommit
   * bytes that fall inside the mmap arena (= Unity's live committed footprint). */
  {
    static size_t rw_b = 0, none_b = 0;
    static unsigned rw_n = 0, none_n = 0, oth_n = 0;
    int in_arena = g_mmap_arena_base &&
                   (uint8_t *)addr >= (uint8_t *)g_mmap_arena_base &&
                   (uint8_t *)addr <  (uint8_t *)g_mmap_arena_base + g_mmap_arena_size;
    if (prot == BIONIC_PROT_NONE)       { none_n++; if (in_arena) none_b += len; }
    else if (prot & BIONIC_PROT_WRITE)  { rw_n++;   if (in_arena) rw_b   += len; }
    else                                  oth_n++;
    if (len >= 4u * 1024 * 1024 || ((rw_n + none_n) & 0x7F) == 0)
      debugPrintf("[mprot] addr=%p len=%zuKB prot=0x%x arena=%d | RW %u/%zuMB NONE %u/%zuMB oth %u  net=%zdMB\n",
                  addr, len >> 10, prot, in_arena, rw_n, rw_b >> 20, none_n, none_b >> 20, oth_n,
                  (ssize_t)(rw_b - none_b) >> 20);
  }
  if (oc_contains(addr)) {
    // OC reservation being committed/decommitted. Unity only ever commits (RW);
    // decommits aren't observed, so PROT_NONE here is left mapped (cheap + safe).
    if (prot != BIONIC_PROT_NONE) {
      mutexLock(&g_mmap_lock);
      oc_commit_locked(addr, len);
      mutexUnlock(&g_mmap_lock);
    }
    return 0;
  }
  return 0;
}
// madvise: no-op. The arena is heap memory, always backed; MADV_DONTNEED leaves the pages as
// they are, as it always did in the heap-backed mode.
int madvise_fake(void *addr, size_t len, int advice) {
  (void)addr; (void)len; (void)advice;
  return 0;
}
