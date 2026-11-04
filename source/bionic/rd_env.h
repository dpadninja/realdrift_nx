/* rd_env.h -- environment variables the port sets for the engine (getenv_fake, shim_libc.c).
 *
 * GC_UNMAP_THRESHOLD=0: il2cpp's Boehm GC is built with USE_MUNMAP. A heap block that stays
 * free for GC_UNMAP_THRESHOLD collections (default 6) is given back by
 * mmap(addr, len, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS), and the GC aborts with
 * "mmap(PROT_NONE) failed" unless the answer is addr. mmap_fake ignores addr and MAP_FIXED
 * (it hands out arena memory), so the first such unmap killed the game: on the console after
 * 13 collections, while loading a race (debug.log 2026-11-03, analysis/research/38-gc-unmap.md).
 * "0" exactly is the GC's documented "never unmap"; mmap_fake never frees the memory anyway,
 * so nothing is lost. Read once, by GC_init.
 * Pure logic, host-tested (tests/c/test_rd_env.c). MIT (see LICENSE). */
#ifndef RD_ENV_H
#define RD_ENV_H

#define RD_ENV_GC_UNMAP_THRESHOLD "GC_UNMAP_THRESHOLD"
#define RD_ENV_GC_NEVER_UNMAP     "0"

/* The fixed value of environment variable `name`, or NULL when the port does not set it (the
 * caller falls back to the real environment). The string is static; the caller must not
 * modify it. Any thread. */
const char *rd_env_fixed(const char *name);

#endif
