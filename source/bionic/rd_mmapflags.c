/* rd_mmapflags.c -- see rd_mmapflags.h. MIT (see LICENSE). */
#include <stddef.h>
#include "rd_mmapflags.h"

int rd_mmap_is_decommit(const void *addr, int prot, int flags) {
  return addr != NULL && prot == BIONIC_PROT_NONE && (flags & BIONIC_MAP_FIXED) &&
         (flags & BIONIC_MAP_ANONYMOUS);
}
