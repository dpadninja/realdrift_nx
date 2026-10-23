/* shim_mmap.h -- shared state of the mmap shims (bionic layer, internal).
 *
 *   shim_mmap.c        the bionic entry points, memalign fallback, read-only file-map cache
 *   shim_mmap_arena.c  the page arena on the newlib heap
 *   shim_mmap_oc.c     the stack-region overcommit window for big PROT_NONE reservations
 * Functions named *_locked must be called with g_mmap_lock held.
 * MIT (see LICENSE). */
#ifndef SHIM_MMAP_H
#define SHIM_MMAP_H

#include <stddef.h>
#include <stdint.h>
#include <switch.h>
#include "mem_layout.h"   /* g_mmap_arena_base/size */


#define BIONIC_MAP_ANONYMOUS 0x20
#define MMAP_PAGE       0x1000u
#define MMAP_BIG_ALIGN  MMAP_ARENA_ALIGN
#define MMAP_BIG_THRESH ((size_t)64 * 1024 * 1024)
#define BIONIC_PROT_NONE 0x0
#define BIONIC_PROT_WRITE 0x2
#define BIONIC_MADV_DONTNEED 4

/* Serialises the arena and the overcommit window (zero-initialised = unlocked libnx mutex). */
extern Mutex g_mmap_lock;

/* ---- heap arena (shim_mmap_arena.c) --------------------------------------------------- */

/* Sets the arena up on first use: the 256 MB-aligned region main.c carved out of the heap, or
 * a heap block if there is none. */
void  mmap_arena_init_locked(void);
/* Reserves len bytes (big requests 256 MB-aligned, so Unity only trims the tail); *got = the
 * bytes actually reserved. NULL if no run of pages is free. */
void *mmap_arena_alloc_locked(size_t len, size_t *got);
/* munmap of an arena range: releases exactly those pages (takes g_mmap_lock itself). */
void  mmap_arena_free(void *addr, size_t len);
/* Bytes reserved in the arena; *size = the arena's size (0 before first use). */
size_t mmap_arena_used_locked(size_t *size);

/* ---- overcommit window (shim_mmap_oc.c) ----------------------------------------------- */

/* Window size in pages; 0 = no window (oc_arena_init failed or was not called). */
extern size_t oc_pages;
/* 1 if addr lies inside the window. */
int   oc_contains(void *addr);
/* Reserves address space in the window (256 MB-aligned like the arena); *got = bytes
 * reserved. NULL when the window is full (the caller falls back to the arena). */
void *oc_alloc_locked(size_t len, size_t *got);
/* Backs [addr,len) with pages of the heap pool (svcMapMemory); committed pages are skipped. */
void  oc_commit_locked(void *addr, size_t len);
/* munmap of a window range: only uncommitted pages are released (committed ones stay mapped,
 * a small bounded leak, so a later reservation cannot collide with a live alias). */
void  oc_free_locked(void *addr, size_t len);
/* Committed bytes of the window; *pool = the commit pool's size. */
size_t oc_committed_locked(size_t *pool);

#endif
