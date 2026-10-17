/* fakefd.h -- fake file-descriptor pipes (fakefd.c).
 *
 * The engine signals between threads with pipe()/read()/write()/close(); Horizon has no such fd
 * table, so libc_shim.c routes those calls on fds handed out here to an in-process pipe.
 * Thread-safe; lazily initialised. MIT (see LICENSE). */
#ifndef FAKEFD_H
#define FAKEFD_H

/* 1 if fd is in the range fakefd hands out (open or not); such fds never reach newlib. */
int  fakefd_is_fake(int fd);
/* Creates a pipe: fds[0] reads, fds[1] writes. Returns 0, or -1 when no slot is free. */
int  fakefd_pipe(int fds[2]);
/* Reads up to n bytes; blocks while the pipe is empty and a writer is open. Returns the count,
 * 0 at end of file (writer closed, buffer drained), or -1 for a bad fd. */
long fakefd_read(int fd, void *buf, unsigned long n);
/* Writes up to n bytes (fewer when the buffer is full; never blocks) and wakes readers.
 * Returns the count, or -1 for a bad fd. */
long fakefd_write(int fd, const void *buf, unsigned long n);
/* Closes one end. Returns 0, or -1 for a bad fd. */
int  fakefd_close(int fd);

#endif
