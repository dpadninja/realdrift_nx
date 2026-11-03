/* sources: source/platform/rd_thrstat.c */
/* Host unit test of rd_thrstat (debug_threads lines). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_platform.h"
#include "rd_thrstat.h"

#define MS_ RD_NS_PER_MS
#define S_  RD_NS_PER_S

static rd_thrstat_thread th(uint64_t tid, const char *name, int prio, uint64_t mask,
                            uint64_t cpu) {
  rd_thrstat_thread t = { .tid = tid, .prio = prio, .ideal = 0, .mask = mask, .cpu_ns = cpu };
  strcpy(t.name, name);
  return t;
}

/* Frames: count, sum, longest, and the slow ones (RD_SLOW_FRAME_MS and up) feed the window
 * line. */
static void test_window(void) {
  rd_thrstat_frames f = { 0 };
  rd_thrstat_frame_add(&f, 10 * MS_);
  rd_thrstat_frame_add(&f, 12 * MS_);
  rd_thrstat_frame_add(&f, 23 * MS_);
  CHECK(f.frames == 3 && f.slow == 1 && f.max_ns == 23 * MS_ && f.sum_ns == 45 * MS_);
  char buf[256];
  rd_thrstat_format_window(10 * S_, &f, buf, sizeof buf);
  CHECK(strcmp(buf, "10.0 s: 3 frames, avg 15.0 ms, max 23 ms, 1 slow") == 0);
  rd_thrstat_frames none = { 0 };
  rd_thrstat_format_window(10 * S_, &none, buf, sizeof buf);
  CHECK(strstr(buf, "0 frames, avg 0.0 ms") != NULL);
}

/* Each core of the process shows its busy share from its idle time; cores outside the mask are
 * left out, and a core that was not measured (no sampler answer, or an idle time above the
 * window) shows "-" instead of a wrong or negative load -- the first log printed nothing at all
 * for the cores the main thread was not on. */
static void test_cores(void) {
  const uint64_t idle[RD_THRSTAT_CORES] = { 2500 * MS_, 10 * S_, RD_THRSTAT_NO_IDLE, 0 };
  char buf[256];
  rd_thrstat_format_cores(0x7, idle, 10 * S_, buf, sizeof buf);
  CHECK(strcmp(buf, "cores busy (all threads): core 0 75.0%, core 1 0.0%, core 2 -") == 0);
  const uint64_t over[RD_THRSTAT_CORES] = { 11 * S_, 0, 0, 0 };
  rd_thrstat_format_cores(0x9, over, 10 * S_, buf, sizeof buf);
  CHECK(strcmp(buf, "cores busy (all threads): core 0 -, core 3 100.0%") == 0);
}

/* The engine threads' CPU since the previous sample, in total and per affinity mask in order of
 * first appearance: this is what shows whether the work piles up on the threads bound to core
 * 0. A thread new in the window counts from zero. */
static void test_masks(void) {
  const rd_thrstat_thread b[] = { th(1, "UnityMain", 0x2c, 0x1, 1 * S_),
                                  th(2, "UnityGfxDevice", 0x3b, 0x1, 2 * S_) };
  const rd_thrstat_thread a[] = { th(1, "UnityMain", 0x2c, 0x1, 7 * S_),
                                  th(2, "UnityGfxDevice", 0x3b, 0x1, 4 * S_),
                                  th(3, "Job.Worker 0", 0x3b, 0x6, 1 * S_) };
  char buf[256];
  rd_thrstat_format_masks(b, 2, a, 3, 10 * S_, buf, sizeof buf);
  CHECK(strcmp(buf, "engine threads: 3, CPU 90.0% of one core (mask 0x1 80.0%, "
                    "mask 0x6 10.0%)") == 0);
}

/* Masks beyond RD_THRSTAT_MASKS are summed as "other" instead of overflowing the table. */
static void test_masks_other(void) {
  rd_thrstat_thread a[RD_THRSTAT_MASKS + 2];
  for (int i = 0; i < RD_THRSTAT_MASKS + 2; i++)
    a[i] = th((uint64_t)i + 1, "T", 0x3b, (uint64_t)i + 1, 1 * S_);
  char buf[512];
  rd_thrstat_format_masks(NULL, 0, a, RD_THRSTAT_MASKS + 2, 100 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "engine threads: 10, CPU 10.0% of one core (mask 0x1 1.0%") == buf);
  CHECK(strstr(buf, "mask 0x8 1.0%, other 2.0%)") != NULL);
}

/* The busiest threads come first; under 1% of the window is left out; max caps the list. */
static void test_busiest(void) {
  const rd_thrstat_thread b[] = { th(1, "A", 0x2c, 1, 0), th(2, "B", 0x3b, 1, 0),
                                  th(3, "C", 0x3b, 1, 0), th(4, "D", 0x3b, 1, 0) };
  const rd_thrstat_thread a[] = { th(1, "A", 0x2c, 1, 1 * S_), th(2, "B", 0x3b, 1, 50 * MS_),
                                  th(3, "C", 0x3b, 1, 3 * S_), th(4, "D", 0x3b, 1, 2 * S_) };
  int order[4];
  CHECK(rd_thrstat_busiest(b, 4, a, 4, 10 * S_, order, 4) == 3);
  CHECK(order[0] == 2 && order[1] == 3 && order[2] == 0);
  CHECK(rd_thrstat_busiest(b, 4, a, 4, 10 * S_, order, 2) == 2);
  CHECK(order[0] == 2 && order[1] == 3);
}

/* A thread's line names its scheduling (the facts the analysis is about) and its CPU share; a
 * counter that went back (a reused tid) does not underflow. */
static void test_thread(void) {
  const rd_thrstat_thread b = th(2, "UnityGfxDevice", 0x3b, 0x1, 2 * S_);
  const rd_thrstat_thread a = th(2, "UnityGfxDevice", 0x3b, 0x1, 4 * S_);
  char buf[256];
  rd_thrstat_format_thread(&b, &a, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, "UnityGfxDevice") == buf);
  CHECK(strstr(buf, "prio 0x3b ideal 0 mask 0x1: 20.0%") != NULL);
  rd_thrstat_format_thread(NULL, &a, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, ": 40.0%") != NULL);
  rd_thrstat_format_thread(&a, &b, 10 * S_, buf, sizeof buf);
  CHECK(strstr(buf, ": 0.0%") != NULL);
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
  RUN(test_cores);
  RUN(test_masks);
  RUN(test_masks_other);
  RUN(test_busiest);
  RUN(test_thread);
  RUN(test_sync);
  RUN(test_truncates);
  return g_failures != 0;
}
