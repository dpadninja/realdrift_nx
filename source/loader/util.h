/* util.h -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

/* printf into debug.log (buffered; lines the debug_log=0 filter drops are skipped, urgent ones
 * are written at once, see rd_log.h). Thread-safe. Always returns 0. */
int debugPrintf(char *text, ...);

/* CPU boost on (1785 MHz, FastLoad: the GPU drops to 76.8 MHz) or back to normal. */
void cpu_boost(int on);
/* Pushes buffered debug.log lines to the SD card (clock thread ~1/s, and at exit). */
void debug_log_flush(void);


/* Point TPIDR_EL0 at a zeroed per-thread block so the engine's stack-protector
 * prologues (which read the canary from TPIDR_EL0+0x28) have a valid, stable
 * bionic TLS. `buf` must outlive the thread. libnx keeps its own thread state in
 * TPIDR_RO_EL0, so commandeering TPIDR_EL0 is safe. EACH THREAD NEEDS ITS OWN
 * BLOCK -- sharing one corrupts the guard slot across threads. */
#define BIONIC_TLS_SIZE 0x400
#define BIONIC_TLS_TP_OFFSET 0x200   /* tp points into the block: headroom for any negative bionic TLS slots */
void install_bionic_tls(void *buf);

#endif
