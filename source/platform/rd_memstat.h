/* rd_memstat.h -- memory report for config.txt debug_mem: a snapshot of where the process's
 * memory is (the newlib heap that mesa's textures and buffers come from, the mmap arena and
 * overcommit window that Unity and il2cpp map, the system's count), its "[mem]" log line,
 * when to take one, and which GL errors after an allocating GL call get logged.
 * Pure logic, host-tested (tests/c/test_rd_memstat.c); the snapshot is filled by
 * rd_memwatch.c. MIT (see LICENSE). */
#ifndef RD_MEMSTAT_H
#define RD_MEMSTAT_H

#include <stddef.h>
#include <stdint.h>

/* One snapshot, in bytes. */
typedef struct {
  uint64_t heap_size;      /* the newlib heap (fake_heap_start .. fake_heap_end) */
  uint64_t heap_top;       /* the part malloc has taken from it so far (mallinfo.arena) */
  uint64_t heap_inuse;     /* allocated blocks (mallinfo.uordblks) */
  uint64_t heap_top_free;  /* the free block at the top of what malloc took (mallinfo.keepcost) */
  uint64_t arena_used, arena_size;      /* mmap page arena: reserved / total */
  uint64_t oc_committed, oc_pool;       /* overcommit window: committed pages / pool */
  uint64_t fallback;       /* mmaps served from the newlib heap after the arena filled */
  uint64_t sys_used, sys_total;         /* svcGetInfo Used/TotalMemorySize */
  uint64_t gc_heap, gc_used;            /* the managed (Boehm) heap and its used part */
  int      gc_state;                    /* RD_MEM_GC_ON / _OFF, or _UNKNOWN (no il2cpp yet) */
} rd_memstat;

#define RD_MEM_GC_UNKNOWN 0
#define RD_MEM_GC_ON      1
#define RD_MEM_GC_OFF     2

/* Heap bytes malloc can still hand out: the untouched rest plus the freed blocks. Fragments
 * count too, so a large texture may fail before this reaches 0. */
uint64_t rd_memstat_heap_avail(const rd_memstat *s);

/* The largest block malloc can surely hand out: the free top block plus the untouched rest
 * (the freed blocks below it may be smaller pieces). When it is much smaller than
 * rd_memstat_heap_avail, the heap is fragmented. */
uint64_t rd_memstat_heap_contig(const rd_memstat *s);

/* The "[mem]" line (no prefix, no newline): in-use heap and its change since `prev` (NULL =
 * none), what is still available and how much of it is one block at the top, the lowest
 * availability seen (`low`), arena, overcommit window, fallback maps, the system's count and,
 * once known, the managed heap (used/size, collections on or OFF), in MB. Returns snprintf's result. */
int rd_memstat_format(const rd_memstat *s, const rd_memstat *prev, uint64_t low, char *buf,
                      size_t n);

/* GL objects and GPU memory counted since start (debug_mem): texture and buffer names the
 * engine generated and deleted, and the nvmap registrations (every texture and buffer mesa
 * allocates) created and closed, with their bytes. Live = created - deleted. */
typedef struct {
  uint64_t tex_made, tex_freed;     /* glGenTextures / glDeleteTextures names */
  uint64_t buf_made, buf_freed;     /* glGenBuffers / glDeleteBuffers names */
  uint64_t maps_made, maps_freed;   /* nvMapCreate (succeeded) / nvMapClose */
  uint64_t map_bytes_made, map_bytes_freed;
} rd_globj;

/* The "[mem] GL objects" line (no prefix, no newline): live textures, buffers and nvmap
 * memory, and what was created and deleted since `prev` (NULL = since start). Returns
 * snprintf's result. */
int rd_memstat_format_globj(const rd_globj *g, const rd_globj *prev, char *buf, size_t n);

/* Why a snapshot is due after `frame` (which took frame_ns): "every 10 s" every
 * RD_MEM_LOG_FRAMES, "after a load" when the frame took at least RD_MEM_LOAD_NS (scene loads
 * allocate and free most), else NULL. */
const char *rd_memstat_due(uint32_t frame, uint64_t frame_ns);

/* 1 if the n-th GL error (1-based), `err`, is logged: GL_OUT_OF_MEMORY always (it is what
 * the report is for), others the first RD_MEM_GL_LOG_FIRST, then every RD_MEM_GL_LOG_EVERY-th,
 * so a stream of them keeps a trace without flooding the log. */
int rd_memstat_gl_logged(uint32_t n, uint32_t err);

/* Name of a GL error code ("GL_OUT_OF_MEMORY", ...; "0x..." for unknown ones, in a static
 * buffer). */
const char *rd_memstat_gl_error_name(uint32_t err);

#endif
