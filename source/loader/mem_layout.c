/* mem_layout.c -- the process memory layout (see mem_layout.h).
 *
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; split out of main.c for realdrift_nx.
 * MIT (see LICENSE). */
#include <malloc.h>
#include <switch.h>

#include "config.h"
#include "libc_shim.h"   /* oc_arena_init */
#include "mem_layout.h"
#include "so_util.h"     /* ALIGN_MEM */
#include "util.h"

void *heap_so_base = NULL;
size_t heap_so_limit = 0;

void  *g_mmap_arena_base = NULL;
size_t g_mmap_arena_size = 0;

/* libunity ~19M + libil2cpp ~48M + headroom for relocated segments */
#define SO_REGION_BYTES (240u * 1024 * 1024)

/* The largest free hole of the process's stack region, start aligned to MMAP_ARENA_ALIGN and
 * at most `want` bytes; *out_size = its size, or NULL and 0 if there is none. Logs the holes. */
static void *oc_find_stack_window(size_t want, size_t *out_size) {
  *out_size = 0;
  u64 sbase = 0, ssize = 0;
  svcGetInfo(&sbase, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&ssize, InfoType_StackRegionSize,    CUR_PROCESS_HANDLE, 0);
  if (!sbase || !ssize) return NULL;
  u64 end = sbase + ssize, a = sbase, best_a = 0, best_l = 0;
  int holes = 0, mapped = 0;
  while (a < end) {
    MemoryInfo mi; u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) break;
    u64 ms = mi.addr, me = mi.addr + mi.size;
    if (me <= a) break;
    if (mi.type == MemType_Unmapped) {
      u64 hs = ms < sbase ? sbase : ms, he = me > end ? end : me;
      if (he > hs) {
        if (he - hs > best_l) { best_l = he - hs; best_a = hs; }
        if (holes < 8)
          debugPrintf("[oc] stack hole %d: %p .. %p (%u MB)\n",
                      holes++, (void *)hs, (void *)he, (unsigned)((he - hs) >> 20));
      }
    } else mapped++;
    a = me;
  }
  debugPrintf("[oc] stack scan: base=%p size=%u MB, %d holes, %d mapped spans, largest=%u MB\n",
              (void *)sbase, (unsigned)(ssize >> 20), holes, mapped, (unsigned)(best_l >> 20));
  if (!best_a) return NULL;
  u64 aligned = (best_a + (MMAP_ARENA_ALIGN - 1)) & ~(MMAP_ARENA_ALIGN - 1);
  if (aligned >= best_a + best_l) return NULL;
  u64 avail = ((best_a + best_l) - aligned) & ~(MMAP_ARENA_ALIGN - 1);
  if (!avail) return NULL;
  if (avail > want) avail = want;
  *out_size = avail;
  return (void *)aligned;
}

/* Called by libnx before main() (overrides its default): takes the free memory as heap and splits
 * it into the newlib heap, the .so region (SO_REGION_BYTES, at most half) and, when there is
 * room, the mmap arena (MMAP_ARENA_RESERVE, capped at 30 % and leaving newlib >= 384 MB). */
void __libnx_initheap(void);
void __libnx_initheap(void) {
  void *addr;
  size_t size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  const size_t MB = 1024 * 1024;
  size_t so_zone = SO_REGION_BYTES;
  if (so_zone > size / 2)
    so_zone = size / 2;

  extern char *fake_heap_start;
  extern char *fake_heap_end;

  const size_t big_align    = MMAP_ARENA_ALIGN;
  const size_t newlib_floor = 384 * MB;
  size_t arena_sz = MMAP_ARENA_RESERVE;
  size_t fake_heap_size;

  if (size > so_zone + big_align + newlib_floor + 256 * MB) {
    size_t avail = size - so_zone - big_align - newlib_floor;
    if (arena_sz > avail) arena_sz = avail & ~(big_align - 1);
    size_t usable    = size - so_zone - big_align;
    size_t arena_cap = ((usable * 30) / 100) & ~(big_align - 1);
    if (arena_sz > arena_cap) arena_sz = arena_cap;
    fake_heap_size = size - so_zone - arena_sz - big_align;
  } else {
    fake_heap_size = (size > so_zone) ? size - so_zone : size / 2;
    arena_sz = 0;
  }

  fake_heap_start = (char *)addr;
  fake_heap_end   = (char *)addr + fake_heap_size;

  heap_so_base  = (void *)ALIGN_MEM((uintptr_t)addr + fake_heap_size, 0x1000);
  heap_so_limit = so_zone;

  if (arena_sz) {
    g_mmap_arena_base = (void *)ALIGN_MEM((uintptr_t)heap_so_base + so_zone, big_align);
    g_mmap_arena_size = arena_sz;
  }
}

void mem_log_boot(void) {
  extern char *fake_heap_start, *fake_heap_end;
  debugPrintf("[boot] mem: newlib=%u MB, mmap arena=%u MB @ %p\n",
              (unsigned)((fake_heap_end - fake_heap_start) / (1024 * 1024)),
              (unsigned)(g_mmap_arena_size / (1024 * 1024)), g_mmap_arena_base);
  u64 tot = 0, used = 0;
  svcGetInfo(&tot,  InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used, InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
  debugPrintf("[boot] phys: total=%u MB used=%u MB free=%u MB\n",
              (unsigned)(tot >> 20), (unsigned)(used >> 20), (unsigned)((tot - used) >> 20));
}

void mem_arm_overcommit(void) {
  void *pool = NULL;
  size_t winsz = 0;
  void *win = oc_find_stack_window(OC_WINDOW_BYTES, &winsz);
  VirtmemReservation *rv = NULL;
  if (win && winsz) {
    virtmemLock();
    rv = virtmemAddReservation(win, winsz);
    virtmemUnlock();
  }
  if (win && rv && winsz) {
    pool = memalign(0x1000, OC_POOL_BYTES);
    if (pool && oc_arena_init(win, winsz, pool, OC_POOL_BYTES))
      debugPrintf("[oc] ARMED: window %u MB @ %p, pool %u MB @ %p\n",
                  (unsigned)(winsz >> 20), win, (unsigned)(OC_POOL_BYTES >> 20), pool);
    else
      debugPrintf("[oc] DISABLED: pool=%p init failed -> heap-backed only\n", pool);
  } else {
    debugPrintf("[oc] DISABLED: no usable stack hole -> heap-backed only\n");
  }
}
