/* rd_platform.h -- timing and size constants of the Switch side of the port, and the frame
 * clock the main loop publishes. Game identity/paths: rd_game.h.
 * MIT (see LICENSE). */
#ifndef RD_PLATFORM_H
#define RD_PLATFORM_H

#include <stdint.h>
#include <time.h>

/* ---- time ---------------------------------------------------------------------------- */
#define RD_NS_PER_S        1000000000ull
#define RD_NS_PER_MS       1000000ull
#define RD_TARGET_HZ       60
#define RD_FRAME_NS        ((RD_NS_PER_S + RD_TARGET_HZ / 2) / RD_TARGET_HZ)  /* 16666667 ns */
#define RD_SLOW_FRAME_MS   20      /* a frame this long is a visibly dropped frame -> [perf] */

/* CLOCK_MONOTONIC in ns (the same clock on the console and on the host tests). */
static inline uint64_t rd_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * RD_NS_PER_S + (uint64_t)ts.tv_nsec;
}

/* ---- main-loop cadences (in frames) ------------------------------------------------- */
#define RD_LOG_FIRST_FRAMES     5     /* log "frame N rendered" for the first frames ...  */
#define RD_LOG_FRAME_EVERY      120   /* ... and then every 2 s                           */
#define RD_DRIVE_RETRY_FRAMES   30    /* retry the il2cpp hooks until the game is loaded  */
#define RD_SD_FLUSH_FRAMES      120   /* commit pending SD writes (saves) ~every 2 s      */
#define RD_CLOCK_LOG_FRAMES     3600  /* log the real CPU/GPU/EMC clocks ~every minute     */
#define RD_SMOKE_SCAN_FRAMES    60    /* re-apply smoke settings in a race ~every second  */
#define RD_GLTRACE_FRAMES       60    /* debug_gl_trace: frames averaged by one capture    */
#define RD_GLTRACE_PERIOD_FRAMES 600  /* debug_gl_trace: one capture ~every 10 s           */
#define RD_FPS_WINDOW_NS        (RD_NS_PER_S / 2)  /* FPS overlay refresh (rd_fps.h) */
#define RD_AUDIO_WATCH_FRAMES   60    /* debug_audio: check the SDL audio callback ~every 1 s */
#define RD_LOAD_PROFILE_NS      RD_NS_PER_S        /* debug_load: frames this long are profiled */
#define RD_MEM_LOG_FRAMES       600   /* debug_mem: a [mem] line ~every 10 s (rd_memstat.h) */
#define RD_MEM_LOAD_NS          RD_NS_PER_S        /* debug_mem: a frame this long is a load */
#define RD_MEM_GL_LOG_FIRST     20    /* debug_mem: GL errors logged one by one ...        */
#define RD_MEM_GL_LOG_EVERY     100   /* ... then every 100th                              */
/* A per-frame hook that ran within this many frames counts as "active" (in a race). */
#define RD_ACTIVE_WINDOW_FRAMES 10u

/* ---- I/O buffers ----------------------------------------------------------------------- */
#define RD_LOG_BUFFER_BYTES            (64 * 1024)   /* debug.log, flushed ~1/s (rd_log.h) */
#define RD_ARCHIVE_STDIO_BUFFER_BYTES  (128 * 1024)  /* fopen of base.apk / the OBB */
#define RD_PATH_MAX                    768           /* path buffers on the SD card */

/* Unity probes case sensitivity by creating CASESENSITIVETEST<guid> in the data folder;
 * libc_shim.c redirects every probe to one scratch file, main.c sweeps strays at boot. */
#define RD_CASETEST_PREFIX  "CASESENSITIVETEST"
#define RD_CASETEST_SCRATCH ".casetest"

/* ---- screens --------------------------------------------------------------------------- */
/* The touch panel always reports in its own 1280x720 space, whatever the render size is
 * (nx_pointer.c scales it); a docked Switch has no touch screen. */
#define RD_TOUCH_PANEL_W   1280
#define RD_TOUCH_PANEL_H   720

/* ---- frame clock (main.c) -------------------------------------------------------------- */
uint32_t port_frame_count(void);            /* frames rendered so far */
extern volatile uint64_t g_rd_frame_start_ns; /* rd_now_ns() when the current frame began */
extern volatile int      g_rd_in_frame;       /* 1 while the engine renders a frame */

#endif
