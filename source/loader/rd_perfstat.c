/* rd_perfstat.c -- see rd_perfstat.h. MIT (see LICENSE). */
#include "rd_perfstat.h"

unsigned nx_stat_open = 0, nx_stat_commit = 0, nx_stat_mmap = 0, nx_stat_gcstop = 0;
volatile unsigned long long nx_stat_read_bytes = 0, nx_stat_read_ns = 0;
volatile unsigned long long nx_stat_swap_ns = 0, nx_stat_glwait_ns = 0;
volatile unsigned long long nx_stat_dequeue_ns = 0, nx_stat_fence_ns = 0;
volatile unsigned long long nx_stat_fopen = 0, nx_stat_fread = 0, nx_stat_fseek = 0, nx_stat_read = 0;
volatile unsigned long long nx_stat_sd_open = 0, nx_stat_sd_open_ns = 0;
volatile unsigned long long nx_stat_sd_read = 0, nx_stat_sd_read_bytes = 0, nx_stat_sd_read_ns = 0;
volatile unsigned long long nx_stat_cache_hit = 0, nx_stat_cache_miss = 0;
volatile unsigned long long nx_stat_readlog_hit = 0;
volatile unsigned long long nx_stat_write = 0, nx_stat_write_bytes = 0, nx_stat_write_ns = 0;
volatile unsigned long long nx_stat_commit_ns = 0, nx_stat_start_ns = 0;
