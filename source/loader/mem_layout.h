/* mem_layout.h -- the process memory layout, set up before main() runs.
 *
 * __libnx_initheap (libnx calls it before main) takes all free memory as the heap and splits it:
 *   [ newlib heap | region for the game's .so files | 256 MB-aligned mmap arena ]
 * The .so region is filled by mem_so_load (the game libraries, then mod.so files); the arena serves mmap
 * (bionic/shim_mmap_arena.c). Later, main() arms the stack-region overcommit window for
 * Unity's huge PROT_NONE reservations (bionic/shim_mmap_oc.c). Sizes: config.h.
 * MIT (see LICENSE). */
#ifndef MEM_LAYOUT_H
#define MEM_LAYOUT_H

#include <stddef.h>

/* Next free address and remaining bytes of the region for the game's .so files. */
extern void  *heap_so_base;
extern size_t heap_so_limit;

/* The mmap arena carved out of the heap (NULL / 0 when the heap was too small: shim_mmap then
 * falls back to a smaller memalign arena). */
extern void  *g_mmap_arena_base;
extern size_t g_mmap_arena_size;

/* Loads the ELF at `path` into the next part of the .so region (so_load) and advances the
 * region past it. Imports are not resolved and the module is not finalized: the caller does
 * both. Returns 0, or so_load's negative error. */
struct so_module;
int mem_so_load(struct so_module *mod, const char *path);

/* Logs the heap and arena sizes and the process's physical memory use. */
void mem_log_boot(void);

/* Finds the largest free hole of the stack region, reserves it and arms the overcommit window
 * with a heap pool (oc_arena_init). Logs [oc] ARMED or why it stays disabled; mmap then works
 * from the heap arena alone. */
void mem_arm_overcommit(void);

#endif
