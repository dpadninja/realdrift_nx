/* rd_patches.h -- runtime patches of libunity.so and the background clock thread (rd_patches.c).
 * Patch sites are found by instruction pattern (rd_patchsites.c), not by fixed offsets.
 * MIT (see LICENSE). */
#ifndef RD_PATCHES_H
#define RD_PATCHES_H

#include <stdint.h>

/* Installs every engine patch it can; returns how many applied, *total = how many exist. */
int  rd_install_engine_patches(uintptr_t unity_base, int *total);

/* Fake vsync, debug.log flush, CPU boost while loading, engine clock during scene loads. */
void rd_start_clock_thread(void);

#endif
