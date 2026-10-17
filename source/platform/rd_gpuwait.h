/* rd_gpuwait.h -- display/GPU wait counters (nx_stat_dequeue_ns, nx_stat_fence_ns) and the
 * clock log; see rd_gpuwait.c. */
#ifndef RD_GPUWAIT_H
#define RD_GPUWAIT_H

/* "[perf] clocks (<when>): CPU .. GPU .. EMC .." -- the clocks this process really runs at. */
void rd_log_clocks(const char *when);

#endif
