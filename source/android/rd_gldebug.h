/* rd_gldebug.h -- config.txt debug_mem: the GL errors mesa reports through KHR_debug, pure
 * part (host test tests/c/test_rd_gldebug.c); the GL glue is rd_gldebug_gl.h.
 *
 * rd_memwatch's glGetError check sees an error only at the next allocating call, so "after
 * glBufferData" names where it was noticed, not the call that raised it. mesa's debug output,
 * synchronous, calls back inside the failing call with its own message ("glTexStorage2D(...)"),
 * and the engine's return addresses are still on the stack. One error repeats thousands of
 * times, so each (message id, caller) is logged the first time and then every
 * RD_GLDEBUG_EVERY-th time. MIT (see LICENSE). */
#ifndef RD_GLDEBUG_H
#define RD_GLDEBUG_H

#include <stdint.h>

#define RD_GL_DEBUG_TYPE_ERROR               0x824C
#define RD_GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  0x824E
#define RD_GLDEBUG_SLOTS  128   /* distinct (id, caller) pairs remembered */
#define RD_GLDEBUG_EVERY  500   /* a repeat is logged every this many times */

/* 1 if a debug message of GL type `type` is worth logging (errors, undefined behaviour). */
int rd_gldebug_wanted(uint32_t type);

/* Counts of the (id, caller) pairs seen so far. Zero-initialised = empty. */
typedef struct {
  uint64_t key[RD_GLDEBUG_SLOTS];
  uint32_t count[RD_GLDEBUG_SLOTS];
  uint32_t overflow;   /* occurrences of pairs that found no free slot */
} rd_gldebug_seen;

/* Counts one occurrence of message `id` raised from `caller` (a code address or 0) and returns
 * how many times the pair has been seen, this one included; pairs that no longer fit count
 * together in `overflow`. Not thread-safe: the caller serialises. */
uint32_t rd_gldebug_count(rd_gldebug_seen *s, uint32_t id, uint64_t caller);

/* 1 if the n-th occurrence of a pair is logged: the first, then every RD_GLDEBUG_EVERY-th. */
int rd_gldebug_logged(uint32_t n);

#endif
