/* rd_wdclock.h -- the stall watchdog's time arithmetic (diag.c), host-testable
 * (tests/c/test_rd_wdclock.c). Times are raw system ticks.
 *
 * The watchdog used to report the HOME menu and sleep as stalls: Horizon stops the whole
 * process there, the watchdog thread included, while the system tick keeps running, so on
 * resume it saw hundreds of seconds without a frame. And a thread that stamps its wait start
 * after the watchdog read the clock gave a negative wait, printed as 1537228672.8 s.
 * MIT (see LICENSE). */
#ifndef RD_WDCLOCK_H
#define RD_WDCLOCK_H

#include <stdint.h>

/* Ticks from `since` to `now`; 0 if `since` is unset (0) or not before `now` (another thread
 * stamped it after `now` was read). */
uint64_t rd_wd_elapsed(uint64_t now, uint64_t since);

/* 1 if the watchdog's own sleep, from `prev_poll` to `now`, lasted longer than `limit`: the
 * process was suspended (HOME menu, sleep), so the gap is not a stall. */
int rd_wd_was_suspended(uint64_t prev_poll, uint64_t now, uint64_t limit);

#endif
