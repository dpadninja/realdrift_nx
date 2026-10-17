/* config.h -- loader-wide build settings and the runtime settings object.
 *
 * Game identity and file names: rd_game.h. Switch-side timings and sizes: rd_platform.h.
 * The fake Android device: rd_device.h. config.txt keys: rd_settings.h.
 *
 * MIT license -- see LICENSE.
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "rd_game.h"
#include "rd_settings.h"

/* ---- memory layout (main.c __libnx_initheap, libc_shim.c mmap) -------------------------
 * Inherited from the so-loader lineage. Unity reserves aligned pools by over-mmapping and
 * trimming head/tail; anon mmaps come from an aligned arena with a per-page bitmap, so a
 * sub-range munmap frees only the trimmed pages. */
#define MMAP_ARENA_ALIGN            ((size_t)64 * 1024 * 1024)
#define MMAP_ARENA_RESERVE          ((size_t)1792 * 1024 * 1024)  /* heap-backed cap (28 x 64 MB) */
#define MMAP_ARENA_FALLBACK_BYTES   ((size_t)768 * 1024 * 1024)   /* if the reserve cannot be had */

/* Stack-region overcommit arena (libc_shim.c, main.c). */
#define OC_WINDOW_BYTES     ((size_t)1536 * 1024 * 1024)
#define OC_POOL_BYTES       ((size_t) 384 * 1024 * 1024)

/* ---- logging -------------------------------------------------------------------------- */
/* debug.log on the SD card (RD_LOG_PATH). On while the port is being brought up. */
#define DEBUG_LOG 1

/* Keep "[jni] ..." lines in debug.log (each class.method logged once). Bring-up aid. */
#define RD_LOG_JNI 1

/* ---- runtime state -------------------------------------------------------------------- */
extern int screen_width;    /* the render resolution, picked at boot (rd_pick_resolution) */
extern int screen_height;

typedef rd_settings Config;   /* config.txt (rd_settings.h) */
extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
