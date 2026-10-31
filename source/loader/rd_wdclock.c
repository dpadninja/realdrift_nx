/* rd_wdclock.c -- see rd_wdclock.h. MIT (see LICENSE). */
#include "rd_wdclock.h"

uint64_t rd_wd_elapsed(uint64_t now, uint64_t since) {
  return (since && since < now) ? now - since : 0;
}

int rd_wd_was_suspended(uint64_t prev_poll, uint64_t now, uint64_t limit) {
  return rd_wd_elapsed(now, prev_poll) > limit;
}
