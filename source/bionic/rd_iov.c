/* rd_iov.c -- see rd_iov.h. MIT (see LICENSE). */
#include "rd_iov.h"

long rd_writev_via(rd_write_fn wr, int fd, const rd_iovec *iov, int iovcnt) {
  if (iovcnt < 0 || (iovcnt > 0 && !iov)) return -1;
  long total = 0;
  for (int i = 0; i < iovcnt; i++) {
    if (iov[i].iov_len == 0) continue;
    long n = wr(fd, iov[i].iov_base, iov[i].iov_len);
    if (n < 0) return total ? total : -1;
    total += n;
    if ((size_t)n < iov[i].iov_len) break;
  }
  return total;
}
