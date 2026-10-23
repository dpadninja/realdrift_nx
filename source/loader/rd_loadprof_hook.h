/* rd_loadprof_hook.h -- config.txt debug_load: samples the file counters (rd_perfstat.h) and the
 * engine threads' CPU time (diag.h) around each frame and logs the two "[load]" lines of
 * rd_loadprof.h for frames of at least RD_LOAD_PROFILE_NS. Main loop only.
 * No host test: counter sampling; the lines are rd_loadprof (tested).
 * MIT (see LICENSE). */
#ifndef RD_LOADPROF_HOOK_H
#define RD_LOADPROF_HOOK_H

#include <stdint.h>

/* Before the engine's frame. */
void rd_loadprof_begin(void);

/* After it; frame_ns = how long it took. */
void rd_loadprof_end(uint64_t frame_ns);

#endif
