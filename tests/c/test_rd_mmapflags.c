/* sources: source/bionic/rd_mmapflags.c */
/* Host unit test of rd_mmapflags (the mmap request answered in place). MIT (see LICENSE). */
#include <stddef.h>
#include "check.h"
#include "rd_mmapflags.h"

#define MAP_PRIVATE_ 0x02   /* bionic MAP_PRIVATE, as Boehm GC passes it */

/* The exact call of Boehm's GC_unmap (libil2cpp +0x751c88: prot 0, flags 0x32) is a decommit:
 * answering it with any other address is what aborted the game. */
static void test_gc_unmap_is_decommit(void) {
  void *block = (void *)0x28000000;
  CHECK(rd_mmap_is_decommit(block, BIONIC_PROT_NONE, 0x32));
  CHECK(rd_mmap_is_decommit(block, BIONIC_PROT_NONE,
                            MAP_PRIVATE_ | BIONIC_MAP_FIXED | BIONIC_MAP_ANONYMOUS));
}

/* Everything else still gets fresh memory as before: a reservation without MAP_FIXED (Unity's
 * pools), a fixed map that wants access (it would need zeroed pages), a fixed file map, and
 * MAP_FIXED at NULL. */
static void test_others_are_new_maps(void) {
  void *block = (void *)0x28000000;
  CHECK(!rd_mmap_is_decommit(block, BIONIC_PROT_NONE, MAP_PRIVATE_ | BIONIC_MAP_ANONYMOUS));
  CHECK(!rd_mmap_is_decommit(NULL, BIONIC_PROT_NONE, MAP_PRIVATE_ | BIONIC_MAP_ANONYMOUS));
  CHECK(!rd_mmap_is_decommit(block, 0x3, MAP_PRIVATE_ | BIONIC_MAP_FIXED | BIONIC_MAP_ANONYMOUS));
  CHECK(!rd_mmap_is_decommit(block, BIONIC_PROT_NONE, MAP_PRIVATE_ | BIONIC_MAP_FIXED));
  CHECK(!rd_mmap_is_decommit(NULL, BIONIC_PROT_NONE,
                             MAP_PRIVATE_ | BIONIC_MAP_FIXED | BIONIC_MAP_ANONYMOUS));
}

int main(void) {
  RUN(test_gc_unmap_is_decommit);
  RUN(test_others_are_new_maps);
  return g_failures != 0;
}
