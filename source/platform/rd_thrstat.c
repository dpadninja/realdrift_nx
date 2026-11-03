/* rd_thrstat.c -- see rd_thrstat.h. MIT (see LICENSE). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rd_platform.h"
#include "rd_thrstat.h"

/* Tenths of a percent of `part` in `whole`. */
static unsigned permille(uint64_t part, uint64_t whole) {
  return whole ? (unsigned)((part * 1000u + whole / 2) / whole) : 0;
}

/* snprintf that appends at *len and keeps *len inside n. */
static void put(char *buf, size_t n, size_t *len, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static void put(char *buf, size_t n, size_t *len, const char *fmt, ...) {
  if (*len >= n) return;
  va_list ap;
  va_start(ap, fmt);
  const int r = vsnprintf(buf + *len, n - *len, fmt, ap);
  va_end(ap);
  if (r > 0) *len = (size_t)r >= n - *len ? n - 1 : *len + (size_t)r;
}

static const rd_thrstat_thread *find(const rd_thrstat_thread *v, int n, uint64_t tid) {
  for (int i = 0; i < n; i++)
    if (v[i].tid == tid) return &v[i];
  return NULL;
}

/* CPU time between b (or none: a thread new in the window) and a; 0 if the counter went back
 * (a reused tid). */
static uint64_t used(const rd_thrstat_thread *b, const rd_thrstat_thread *a) {
  const uint64_t from = b ? b->cpu_ns : 0;
  return a->cpu_ns > from ? a->cpu_ns - from : 0;
}

/* "12.3%": tenths of a percent. */
#define PCT(p) (p) / 10, (p) % 10

void rd_thrstat_frame_add(rd_thrstat_frames *f, uint64_t frame_ns) {
  f->frames++;
  f->sum_ns += frame_ns;
  if (frame_ns > f->max_ns) f->max_ns = frame_ns;
  if (frame_ns >= (uint64_t)RD_SLOW_FRAME_MS * RD_NS_PER_MS) f->slow++;
}

void rd_thrstat_sync_diff(const rd_thrstat_sync *a, const rd_thrstat_sync *b,
                          rd_thrstat_sync *d) {
  const uint64_t *pa = (const uint64_t *)a, *pb = (const uint64_t *)b;
  uint64_t *pd = (uint64_t *)d;
  for (size_t i = 0; i < sizeof *d / sizeof(uint64_t); i++) pd[i] = pb[i] - pa[i];
}

int rd_thrstat_format_window(uint64_t window_ns, const rd_thrstat_frames *f, char *buf,
                             size_t n) {
  size_t len = 0;
  buf[0] = 0;
  const uint64_t avg = f->frames ? f->sum_ns / f->frames : 0;
  put(buf, n, &len, "%llu.%llu s: %u frames, avg %llu.%llu ms, max %llu ms, %u slow",
      (unsigned long long)(window_ns / RD_NS_PER_S),
      (unsigned long long)(window_ns % RD_NS_PER_S / (RD_NS_PER_S / 10)), f->frames,
      (unsigned long long)(avg / RD_NS_PER_MS),
      (unsigned long long)(avg % RD_NS_PER_MS / (RD_NS_PER_MS / 10)),
      (unsigned long long)(f->max_ns / RD_NS_PER_MS), f->slow);
  return (int)len;
}

int rd_thrstat_format_cores(uint64_t mask, const uint64_t idle_ns[RD_THRSTAT_CORES],
                            uint64_t window_ns, char *buf, size_t n) {
  size_t len = 0;
  buf[0] = 0;
  put(buf, n, &len, "cores busy (all threads):");
  int first = 1;
  for (int c = 0; c < RD_THRSTAT_CORES; c++) {
    if (!(mask & (1ull << c))) continue;
    put(buf, n, &len, "%s core %d ", first ? "" : ",", c);
    first = 0;
    if (idle_ns[c] == RD_THRSTAT_NO_IDLE || idle_ns[c] > window_ns) {
      put(buf, n, &len, "-");
      continue;
    }
    const unsigned busy = 1000u - permille(idle_ns[c], window_ns);
    put(buf, n, &len, "%u.%u%%", PCT(busy));
  }
  return (int)len;
}

int rd_thrstat_format_masks(const rd_thrstat_thread *before, int nb,
                            const rd_thrstat_thread *after, int na, uint64_t window_ns,
                            char *buf, size_t n) {
  uint64_t masks[RD_THRSTAT_MASKS], per_mask[RD_THRSTAT_MASKS] = { 0 };
  uint64_t total = 0, other = 0;
  int nm = 0;
  for (int i = 0; i < na; i++) {
    const uint64_t u = used(find(before, nb, after[i].tid), &after[i]);
    total += u;
    int m = 0;
    while (m < nm && masks[m] != after[i].mask) m++;
    if (m == nm && nm < RD_THRSTAT_MASKS) masks[nm++] = after[i].mask;
    if (m < nm) per_mask[m] += u;
    else other += u;
  }
  size_t len = 0;
  buf[0] = 0;
  const unsigned p = permille(total, window_ns);
  put(buf, n, &len, "engine threads: %d, CPU %u.%u%% of one core (", na, PCT(p));
  for (int m = 0; m < nm; m++) {
    const unsigned q = permille(per_mask[m], window_ns);
    put(buf, n, &len, "%smask 0x%llx %u.%u%%", m ? ", " : "", (unsigned long long)masks[m],
        PCT(q));
  }
  if (other) {
    const unsigned q = permille(other, window_ns);
    put(buf, n, &len, "%sother %u.%u%%", nm ? ", " : "", PCT(q));
  }
  put(buf, n, &len, ")");
  return (int)len;
}

int rd_thrstat_busiest(const rd_thrstat_thread *before, int nb, const rd_thrstat_thread *after,
                       int na, uint64_t window_ns, int *order, int max) {
  int idx[RD_THRSTAT_MAX_THREADS];
  uint64_t u[RD_THRSTAT_MAX_THREADS];
  int k = 0;
  for (int i = 0; i < na && k < RD_THRSTAT_MAX_THREADS; i++) {
    const uint64_t t = used(find(before, nb, after[i].tid), &after[i]);
    if (permille(t, window_ns) < 10) continue;
    int at = k++;
    for (; at > 0 && u[at - 1] < t; at--) { u[at] = u[at - 1]; idx[at] = idx[at - 1]; }
    u[at] = t;
    idx[at] = i;
  }
  if (k > max) k = max;
  for (int i = 0; i < k; i++) order[i] = idx[i];
  return k;
}

int rd_thrstat_format_thread(const rd_thrstat_thread *before, const rd_thrstat_thread *after,
                             uint64_t window_ns, char *buf, size_t n) {
  size_t len = 0;
  buf[0] = 0;
  const unsigned p = permille(used(before, after), window_ns);
  put(buf, n, &len, "%-20s prio 0x%02x ideal %d mask 0x%llx: %u.%u%%", after->name,
      (unsigned)after->prio, (int)after->ideal, (unsigned long long)after->mask, PCT(p));
  return (int)len;
}

int rd_thrstat_format_sync(const rd_thrstat_sync *d, uint64_t window_ns, char *buf, size_t n) {
  size_t len = 0;
  buf[0] = 0;
  const uint64_t ms = window_ns / RD_NS_PER_MS ? window_ns / RD_NS_PER_MS : 1;
#define RATE(x) (unsigned long long)((x) * 1000u / ms)
  put(buf, n, &len,
      "per second: sem_timedwait %llu (1 ms polls %llu, timeouts %llu), sem_wait %llu, sem_post "
      "%llu, mutex_timedlock polls "
      "%llu, cond waits %llu (16 ms cap %llu), futex waits %llu (16 ms cap %llu), futex wakes "
      "%llu, sched_yield %llu",
      RATE(d->sem_timedwaits), RATE(d->sem_polls), RATE(d->sem_timeouts), RATE(d->sem_waits),
      RATE(d->sem_posts), RATE(d->mutex_polls),
      RATE(d->cond_waits), RATE(d->cond_capped), RATE(d->futex_waits), RATE(d->futex_capped),
      RATE(d->futex_wakes), RATE(d->yields));
#undef RATE
  return (int)len;
}
