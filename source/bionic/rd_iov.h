/* rd_iov.h -- writev() on top of a plain write(); host-testable (tests/c/test_rd_iov.c).
 * MIT (see LICENSE). */
#ifndef RD_IOV_H
#define RD_IOV_H

#include <stddef.h>

/* Same layout as bionic/Linux struct iovec on arm64. */
typedef struct {
  void  *iov_base;
  size_t iov_len;
} rd_iovec;

typedef long (*rd_write_fn)(int fd, const void *buf, size_t len);

/* writev semantics: buffers are written in order; stops at the first short write.
 * Returns bytes written, or -1 if nothing was written and the first write failed. */
long rd_writev_via(rd_write_fn wr, int fd, const rd_iovec *iov, int iovcnt);

#endif
