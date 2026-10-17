/* sources: source/bionic/rd_iov.c */
/* Host unit test of rd_iov. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_iov.h"

static char   g_out[64];
static size_t g_out_len;
static long   g_limit;      /* max bytes the fake write accepts per call; <0 => fail */
static int    g_calls;

static long fake_write(int fd, const void *buf, size_t len) {
  (void)fd;
  g_calls++;
  if (g_limit < 0) return -1;
  size_t n = len < (size_t)g_limit ? len : (size_t)g_limit;
  memcpy(g_out + g_out_len, buf, n);
  g_out_len += n;
  return (long)n;
}

static void reset(long limit) { memset(g_out, 0, sizeof g_out); g_out_len = 0; g_limit = limit; g_calls = 0; }

/* writev concatenates the buffers in order and skips empty ones without a write call (the
 * engine's logging passes empty iovecs). */
static void test_writes_all_buffers_in_order(void) {
  reset(100);
  rd_iovec iov[] = { { "ab", 2 }, { "", 0 }, { "cde", 3 } };
  CHECK(rd_writev_via(fake_write, 3, iov, 3) == 5);
  CHECK(strcmp(g_out, "abcde") == 0);
  CHECK(g_calls == 2);   /* empty buffer skipped */
}

/* After a short write it stops and returns the bytes written, like POSIX writev; continuing
 * would reorder the output. */
static void test_stops_after_short_write(void) {
  reset(1);
  rd_iovec iov[] = { { "ab", 2 }, { "cd", 2 } };
  CHECK(rd_writev_via(fake_write, 3, iov, 2) == 1);
  CHECK(strcmp(g_out, "a") == 0);
}

/* An error before anything was written is reported as -1, not as 0 bytes. */
static void test_error_on_first_write_returns_minus_one(void) {
  reset(-1);
  rd_iovec iov[] = { { "ab", 2 } };
  CHECK(rd_writev_via(fake_write, 3, iov, 1) == -1);
}

/* Zero buffers write nothing; a NULL array with a count or a negative count is an error and
 * never calls write. */
static void test_zero_and_invalid_counts(void) {
  reset(100);
  CHECK(rd_writev_via(fake_write, 3, NULL, 0) == 0);
  CHECK(rd_writev_via(fake_write, 3, NULL, 1) == -1);
  rd_iovec iov[] = { { "ab", 2 } };
  CHECK(rd_writev_via(fake_write, 3, iov, -1) == -1);
  CHECK(g_calls == 0);
}

int main(void) {
  RUN(test_writes_all_buffers_in_order);
  RUN(test_stops_after_short_write);
  RUN(test_error_on_first_write_returns_minus_one);
  RUN(test_zero_and_invalid_counts);
  return g_failures != 0;
}
