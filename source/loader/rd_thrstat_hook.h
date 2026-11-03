/* rd_thrstat_hook.h -- config.txt debug_threads: samples the engine threads (diag.c), each
 * core's idle time (a sampler thread pinned to each core of the process, started on the first
 * call) and the sync counters (rd_perfstat.h) every RD_THRSTAT_WINDOW_NS and logs the "[thr]"
 * lines of rd_thrstat.h. Main loop thread. MIT (see LICENSE). */
#ifndef RD_THRSTAT_HOOK_H
#define RD_THRSTAT_HOOK_H

#include <stdint.h>

/* Called once per frame with the engine frame's time; logs at the end of each window. */
void rd_thrstat_frame(uint64_t frame_ns);

#endif
