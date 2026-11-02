/* sources: source/platform/rd_thrstat.c */
/* Host unit test of rd_thrstat (debug_threads lines). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_platform.h"
#include "rd_thrstat.h"

#define MS_ RD_NS_PER_MS
#define S_  RD_NS_PER_S

static rd_thrstat_thread th(uint64_t tid, const char *name, int prio, uint64_t c0, uint64_t c1) {
  rd_thrstat_thread t = { .tid = tid, .prio = prio, .ideal = 0, .mask = 1,
                          .core_ns = { c0, c1, 0, 0 } };
  strcpy(t.name, name);
  return t;
}

/* Frames: count, sum, longest, and the slow ones (RD_SLOW_FRAME_MS and up) feed the window
 * line, with core 0's busy share from its idle time. */
static void test_window(void) {
  rd_thrstat_frames f = { 0 };
  rd_thrstat_frame_add(&f, 10 * MS_);
  rd_thrstat_frame_add(&f, 12 * MS_);
  rd_thrstat_frame_add(&f, 23 * MS_);
  CHECK(f.frames == 3 && f.slow == 1 && f.max_ns == 23 * MS_ && f.sum_ns == 45 * MS_);
  char buf[256];
  rd_thrstat_format_window(10 * S_, &f, 2500 * MS_, buf, sizeof buf);
  CHECK(strstr(buf, "10.0 s: 3 frames, avg 15.0 ms, max 23 ms, 1 slow") != NULL);
  CHECK(strstr(buf, "core 0 busy 75.0%") != NULL);
}

/* An idle time that was not measured (above the window) leaves the core 0 part out instead of
 * printing a negative load. */
static void test_window_no_idle(void) {
  rd_thrstat_frames f = { 0 };
  char buf[256];
  rd_thrstat_format_window(10 * S_, &f, 11 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "0 frames, avg 0.0 ms") != NULL);
  CHECK(strstr(buf, "core 0") == NULL);
}

/* Per-core load sums each thread's CPU time on that core since the previous sample; a thread
 * that appeared in the window counts from zero. */
static void test_cores(void) {
  const rd_thrstat_thread b[] = { th(1, "UnityMain", 0x2c, 1 * S_, 0),
                                  th(2, "UnityGfxDevice", 0x3b, 2 * S_, 0) };
  const rd_thrstat_thread a[] = { th(1, "UnityMain", 0x2c, 7 * S_, 0),
                                  th(2, "UnityGfxDevice", 0x3b, 4 * S_, 0),
                                  th(3, "Job.Worker 0", 0x3b, 0, 1 * S_) };
  char buf[256];
  rd_thrstat_format_cores(b, 2, a, 3, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "core 0 80.0%, core 1 10.0%, core 2 0.0%, core 3 0.0%") != NULL);
}

/* The busiest threads come first; under 1% of the window is left out; max caps the list. */
static void test_busiest(void) {
  const rd_thrstat_thread b[] = { th(1, "A", 0x2c, 0, 0), th(2, "B", 0x3b, 0, 0),
                                  th(3, "C", 0x3b, 0, 0), th(4, "D", 0x3b, 0, 0) };
  const rd_thrstat_thread a[] = { th(1, "A", 0x2c, 1 * S_, 0), th(2, "B", 0x3b, 50 * MS_, 0),
                                  th(3, "C", 0x3b, 3 * S_, 0), th(4, "D", 0x3b, 2 * S_, 0) };
  int order[4];
  CHECK(rd_thrstat_busiest(b, 4, a, 4, 10 * S_, order, 4) == 3);
  CHECK(order[0] == 2 && order[1] == 3 && order[2] == 0);
  CHECK(rd_thrstat_busiest(b, 4, a, 4, 10 * S_, order, 2) == 2);
  CHECK(order[0] == 2 && order[1] == 3);
}

/* A thread's line names its scheduling (the facts the analysis is about) and its share per
 * core; a counter that went back (a reused tid) does not underflow. */
static void test_thread(void) {
  const rd_thrstat_thread b = th(2, "UnityGfxDevice", 0x3b, 2 * S_, 5 * S_);
  const rd_thrstat_thread a = th(2, "UnityGfxDevice", 0x3b, 4 * S_, 1 * S_);
  char buf[256];
  rd_thrstat_format_thread(&b, &a, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "UnityGfxDevice") == buf);
  CHECK(strstr(buf, "prio 0x3b ideal 0 mask 0x1: 20.0% c0 20.0%") != NULL);
  CHECK(strstr(buf, " c1") == NULL);
  rd_thrstat_format_thread(NULL, &a, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, ": 50.0% c0 40.0% c1 10.0%") != NULL);
}

/* The sync counters are shown per second of the window. */
static void test_sync(void) {
  const rd_thrstat_sync a = { 1, 2, 3, 0, 0, 4, 5, 6, 7, 8, 9, 10 };
  const rd_thrstat_sync b = { 1001, 20002, 3, 500, 600, 4, 605, 306, 1007, 208, 2009, 10 };
  rd_thrstat_sync d;
  rd_thrstat_sync_diff(&a, &b, &d);
  CHECK(d.sem_timedwaits == 1000 && d.sem_polls == 20000 && d.futex_wakes == 2000);
  char buf[512];
  rd_thrstat_format_sync(&d, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "sem_timedwait 100 (1 ms polls 2000, timeouts 0), sem_wait 50, sem_post 60")
        != NULL);
  CHECK(strstr(buf, "cond waits 60 (16 ms cap 30), futex waits 100 (16 ms cap 20)") != NULL);
  CHECK(strstr(buf, "futex wakes 200, sched_yield 0") != NULL);
}

/* A short buffer is cut, not overrun. */
static void test_truncates(void) {
  const rd_thrstat_sync d = { 0 };
  char buf[16];
  rd_thrstat_format_sync(&d, 10 * S_, buf, sizeof buf);
  CHECK(strlen(buf) == sizeof buf - 1);
}

int main(void) {
  RUN(test_window);
  RUN(test_window_no_idle);
  RUN(test_cores);
  RUN(test_busiest);
  RUN(test_thread);
  RUN(test_sync);
  RUN(test_truncates);
  return g_failures != 0;
}
