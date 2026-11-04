/* rd_mmapflags.h -- bionic mmap flag values and the one mmap request mmap_fake answers in place.
 *
 * il2cpp's Boehm GC (built with USE_MUNMAP) gives a heap block that stayed free for 6
 * collections back with mmap(addr, len, PROT_NONE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS)
 * and aborts with "mmap(PROT_NONE) failed" unless the answer is addr; later it takes the block
 * back with mprotect(RW). In this il2cpp build GC_unmap_old has the 6 compiled in (libil2cpp
 * 5.0.8 +0x751b3c: gc_no + 0xfffa), so GC_UNMAP_THRESHOLD in the environment does not stop it.
 * mmap_fake used to ignore addr and MAP_FIXED, so the first such unmap killed the game, on the
 * console after 11-13 collections while loading a race (analysis/research/38-gc-unmap.md).
 * Such a request is now a decommit in place: the memory stays as it is and addr is returned,
 * the same as mprotect_fake does for PROT_NONE. A free GC block holds stale data either way.
 * Pure logic, host-tested (tests/c/test_rd_mmapflags.c). MIT (see LICENSE). */
#ifndef RD_MMAPFLAGS_H
#define RD_MMAPFLAGS_H

#define BIONIC_PROT_NONE     0x0
#define BIONIC_PROT_WRITE    0x2
#define BIONIC_MAP_FIXED     0x10
#define BIONIC_MAP_ANONYMOUS 0x20

/* 1 if mmap(addr, ..., prot, flags) only decommits address space the caller already holds:
 * anonymous, MAP_FIXED, PROT_NONE and a non-NULL addr. mmap_fake then returns addr unchanged. */
int rd_mmap_is_decommit(const void *addr, int prot, int flags);

#endif
