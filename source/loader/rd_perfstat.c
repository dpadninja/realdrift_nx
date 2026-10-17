/* rd_perfstat.c -- see rd_perfstat.h. MIT (see LICENSE). */
#include "rd_perfstat.h"

unsigned nx_stat_open = 0, nx_stat_commit = 0, nx_stat_mmap = 0, nx_stat_gcstop = 0;
volatile unsigned long long nx_stat_read_bytes = 0, nx_stat_read_ns = 0;
volatile unsigned long long nx_stat_swap_ns = 0, nx_stat_glwait_ns = 0;
volatile unsigned long long nx_stat_dequeue_ns = 0, nx_stat_fence_ns = 0;
